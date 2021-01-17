#include "chp_sim.h"
#include "chp_sim_record.h"

void PauliFrameSimulation::sample(aligned_bits256& out, std::mt19937 &rng) {
    auto rand_bit = std::bernoulli_distribution(0.5);

    PauliStringVal pauli_frame_val(num_qubits);
    PauliStringPtr pauli_frame = pauli_frame_val.ptr();
    size_t result_count = 0;
    for (const auto &cycle : cycles) {
        for (const auto &op : cycle.step1_unitary) {
            pauli_frame.unsigned_conjugate_by(op.name, op.targets);
        }
        for (const auto &collapse : cycle.step2_collapse) {
            if (rand_bit(rng)) {
                pauli_frame.unsigned_multiply_by(collapse.destabilizer);
            }
        }
        for (const auto &measurement : cycle.step3_measure) {
            auto q = measurement.target_qubit;
            out.set_bit(result_count, pauli_frame.get_x_bit(q) ^ measurement.invert);
            result_count++;
        }
        for (const auto &q : cycle.step4_reset) {
            pauli_frame.set_z_bit(q, false);
            pauli_frame.set_x_bit(q, false);
        }
    }
}

PauliFrameSimulation PauliFrameSimulation::recorded_from_tableau_sim(const std::vector<Operation> &operations) {
    constexpr uint8_t PHASE_UNITARY = 0;
    constexpr uint8_t PHASE_COLLAPSED = 1;
    constexpr uint8_t PHASE_RESET = 2;
    PauliFrameSimulation resulting_simulation {};

    for (const auto &op : operations) {
        for (auto q : op.targets) {
            resulting_simulation.num_qubits = std::max(resulting_simulation.num_qubits, q + 1);
        }
        if (op.name == "M") {
            resulting_simulation.num_measurements += op.targets.size();
        }
    }

    PauliFrameSimCycle partial_cycle {};
    std::unordered_map<size_t, uint8_t> qubit_phases {};
    ChpSim sim(resulting_simulation.num_qubits);

    auto start_next_moment = [&](){
        resulting_simulation.cycles.push_back(partial_cycle);
        partial_cycle = {};
        qubit_phases.clear();
    };
    for (const auto &op : operations) {
        if (op.name == "X" || op.name == "Y" || op.name == "Z") {
            sim.func_op(op.name, op.targets);
            continue;
        }
        if (op.name == "TICK") {
            continue;
        }
        if (op.name == "M") {
            for (auto q : op.targets) {
                if (qubit_phases[q] > PHASE_COLLAPSED) {
                    start_next_moment();
                    break;
                }
            }

            auto collapse_results = sim.inspected_collapse(op.targets);
            for (size_t k = 0; k < collapse_results.size(); k++) {
                auto q = op.targets[k];
                const auto &collapse_result = collapse_results[k];
                if (!collapse_result.indexed_words.empty()) {
                    partial_cycle.step2_collapse.emplace_back(collapse_result);
                }
                qubit_phases[q] = PHASE_COLLAPSED;
                partial_cycle.step3_measure.emplace_back(q, collapse_result.sign);
            }
        } else if (op.name == "R") {
            auto collapse_results = sim.inspected_collapse(op.targets);
            for (size_t k = 0; k < collapse_results.size(); k++) {
                auto q = op.targets[k];
                const auto &collapse_result = collapse_results[k];
                if (!collapse_result.indexed_words.empty()) {
                    partial_cycle.step2_collapse.emplace_back(collapse_result);
                }
                partial_cycle.step4_reset.push_back(q);
                qubit_phases[q] = PHASE_RESET;
            }
            sim.reset_many(op.targets);
        } else {
            for (auto q : op.targets) {
                if (qubit_phases[q] > PHASE_UNITARY) {
                    start_next_moment();
                    break;
                }
            }
            partial_cycle.step1_unitary.push_back(op);
            sim.func_op(op.name, op.targets);
        }
    }

    if (partial_cycle.step1_unitary.size()
            || partial_cycle.step2_collapse.size()
            || partial_cycle.step3_measure.size()
            || partial_cycle.step4_reset.size()) {
        resulting_simulation.cycles.push_back(partial_cycle);
    }

    return resulting_simulation;
}

std::ostream &operator<<(std::ostream &out, const PauliFrameSimulation &ps) {
    for (const auto &cycle : ps.cycles) {
        for (const auto &op : cycle.step1_unitary) {
            out << op.name;
            for (auto q : op.targets) {
                out << " " << q;
            }
            out << "\n";
        }
        for (const auto &op : cycle.step2_collapse) {
            out << "RANDOM_INTO_FRAME " << op.destabilizer.str().substr(1) << "\n";
        }
        if (!cycle.step3_measure.empty()) {
            out << "REPORT_FRAME";
            for (const auto &q : cycle.step3_measure) {
                out << " ";
                if (q.invert) {
                    out << "!";
                }
                out << q.target_qubit;
            }
            out << "\n";
        }
        if (!cycle.step4_reset.empty()) {
            out << "R";
            for (const auto &q : cycle.step4_reset) {
                out << " " << q;
            }
            out << "\n";
        }
    }
    return out;
}

std::string PauliFrameSimulation::str() const {
    std::stringstream s;
    s << *this;
    return s.str();
}
