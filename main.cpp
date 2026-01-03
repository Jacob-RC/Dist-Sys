#include "node.cpp"
#include <vector>
#include <thread>
#include <iostream>

int main(){
    std::vector<node *> nodes;
    std::vector<std::thread> node_threads;
    
    for(int i = 0; i < 10; i++){
        nodes.push_back(new node(i));
    }

    // Update the neighbors for all nodes
    for(node * node_ptr : nodes){
        node_ptr->neighbors = nodes;
    }

    for(int i = 0; i < 10; i++){
        node_threads.emplace_back(&node::run, nodes[i]);
    }
    nodes[0]->run_election();

    // Run the simulation for 5 seconds
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // stop simulation
    for(node * node_ptr : nodes) node_ptr->alive = false; 

    int index = 0;
    for(std::thread& thread : node_threads){
        thread.join();
        std::cout << "Node " << index << " closed " << std::endl;
        index++;
    }    
    
    // Verify simulation logic - check log consistency across all nodes
    std::cout << "\n=== Log Consistency Check ===" << std::endl;

    bool logs_consistent = true;

    // Find the maximum log size across all nodes
    size_t max_log_size = 0;
    for (int i = 0; i < 10; i++) {
        max_log_size = std::max(max_log_size, nodes[i]->log.size());
    }

    std::cout << "Max log size: " << max_log_size << std::endl;

    // Print each node's log size
    for (int i = 0; i < 10; i++) {
        std::cout << "Node " << i << " log size: " << nodes[i]->log.size() << std::endl;
    }

    // Check each log entry across all nodes
    for (size_t log_idx = 0; log_idx < max_log_size; log_idx++) {
        // Find the first node that has this log entry to use as reference
        log_entry* reference = nullptr;
        int reference_node = -1;

        for (int node_idx = 0; node_idx < 10; node_idx++) {
            if (log_idx < nodes[node_idx]->log.size()) {
                reference = &nodes[node_idx]->log[log_idx];
                reference_node = node_idx;
                break;
            }
        }

        if (reference == nullptr) {
            continue;  // No node has this entry
        }

        // Compare all other nodes against the reference
        for (int node_idx = 0; node_idx < 10; node_idx++) {
            if (node_idx == reference_node) {
                continue;
            }

            // Check if this node has the entry
            if (log_idx >= nodes[node_idx]->log.size()) {
                std::cout << "MISMATCH: Node " << node_idx << " is missing log entry at index " << log_idx
                          << " (present in node " << reference_node << ")" << std::endl;
                logs_consistent = false;
                continue;
            }

            log_entry& current = nodes[node_idx]->log[log_idx];

            // Check index
            if (current.index != reference->index) {
                std::cout << "MISMATCH at log position " << log_idx << ": Node " << node_idx
                          << " has index=" << current.index << ", but node " << reference_node
                          << " has index=" << reference->index << std::endl;
                logs_consistent = false;
            }

            // Check term
            if (current.term_idx != reference->term_idx) {
                std::cout << "MISMATCH at log position " << log_idx << ": Node " << node_idx
                          << " has term=" << current.term_idx << ", but node " << reference_node
                          << " has term=" << reference->term_idx << std::endl;
                logs_consistent = false;
            }

            // Check entry data
            if (current.entry_data != reference->entry_data) {
                std::cout << "MISMATCH at log position " << log_idx << ": Node " << node_idx
                          << " has data=\"" << current.entry_data << "\", but node " << reference_node
                          << " has data=\"" << reference->entry_data << "\"" << std::endl;
                logs_consistent = false;
            }
        }
    }

    if (logs_consistent) {
        std::cout << "SUCCESS: All node logs are consistent!" << std::endl;
    } else {
        std::cout << "FAILURE: Log inconsistencies detected!" << std::endl;
    }

    std::cout << "=== End Log Check ===\n" << std::endl;

    for(node * node_ptr : nodes) delete node_ptr;
    return 0;
}