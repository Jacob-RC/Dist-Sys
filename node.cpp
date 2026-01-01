#include <algorithm>
#include <thread>
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <iostream>
#include <chrono>
#include <unordered_map>
#include <random>

enum class MessageType {
    HEARTBEAT,
    ELECTION,
    NEWENTRY,
    NEW_ENTRY_ACK,
    COMMIT,
    LEADER_NEW_ENTRY,
    REQUEST_VOTES,
    ELECTION_RESPONSE,
    ELECTION_RESULT,
};

struct msg{
    log_entry message;
    MessageType msg_type;
    int sender_id;
    std::chrono::_V2::steady_clock::time_point timestamp;
};

struct log_entry {
    int index;
    int term_idx;
    std::string entry_data;
};

class node {
    public:
        // Internal node information
        bool alive;
        bool is_leader;
        int node_id;
        std::vector<log_entry> log;
        int latest_LSN;
        int current_term;
        std::chrono::milliseconds timeout;
        std::chrono::_V2::steady_clock::time_point last_timestamp;

        // Internode communication
        std::mutex mailbox_mutex;
        std::deque<msg> mailbox;
        std::vector<node *> neighbors;
        std::unordered_map<int, log_entry> staged_entry;
        int leader_idx;
        int voted_for;

        void run(){
            
            std::cout << "Node: " << node_id << " is running " << std::endl;

            while(alive) {

                bool message_sent = false;

                //Run election if no leader right now
                if(leader_idx == -1){
                    run_election();
                }
                
                auto current_time = std::chrono::steady_clock::now();
                auto duration = current_time - last_timestamp;

                //Otherwise, run election if enough time has passed
                if(duration > timeout){
                    run_election();
                }

                // Handle message recieve logic
                while(!mailbox.empty()){

                    std::lock_guard<std::mutex> lock (mailbox_mutex);
                    msg recieved_msg = mailbox.front();
                    mailbox.pop_front();

                    int recieved_idx = recieved_msg.message.index;
                    MessageType packet_type = recieved_msg.msg_type;
                    

                    if(packet_type== MessageType::NEWENTRY){

                        // Update terms
                        if(recieved_msg.message.term_idx > current_term){
                            current_term = recieved_msg.message.term_idx;
                        }
                        else if (recieved_msg.message.term_idx < current_term){
                            // Ignore requests from a stale term_idx
                            return;
                        }

                        // Find which node sent the message
                        int response_id = recieved_msg.sender_id;
                        node * responder = neighbors[response_id];

                        //Craft the response
                        log_entry new_log = {recieved_idx, -1, "Acknowledge New Log Entry"};
                        msg new_msg = {new_log, MessageType::NEW_ENTRY_ACK, node_id, std::chrono::steady_clock::now()};

                        // Send the message
                        responder->recieve_message(new_msg);

                        //Don't stage heartbeats to be committed
                        if(!recieved_msg.message.entry_data.empty()){
                            // Log down that the entry has been recieved
                            staged_entry[recieved_idx] = recieved_msg.message;
                        }
                    }
                    else if (packet_type == MessageType::COMMIT){
                        // Check to see if there's a corresponding staged entry
                        if(staged_entry.count(recieved_idx) != 0){
                            log_entry fin_msg = staged_entry[recieved_idx];

                            // Finally commit the log into internal storage.
                            log.push_back(fin_msg);
                        }
                    }
                    else if (packet_type == MessageType::NEW_ENTRY_ACK && is_leader){

                        log_entry broadcoast_log = {latest_LSN++, current_term, recieved_msg.message.entry_data};
                        new_log_entry(recieved_msg.message);
                        message_sent = true;

                    }
                    else if (packet_type == MessageType::REQUEST_VOTES){

                        bool res = request_vote_impl(recieved_msg.message);

                        // Send back the acknowledge that we voted for the candidate
                        if(res){
                            voted_for = recieved_msg.sender_id;

                            // Craft response packet
                            log_entry response = {0,0,"ElectionBallot"};
                            msg packet = {response, MessageType::ELECTION_RESPONSE, node_id, std::chrono::steady_clock::now()};

                            // send the response
                            node * responder = neighbors[recieved_msg.sender_id];
                            responder->recieve_message(packet);
                        }
                    }
                    else {
                        // Do nothing (message type was unused)
                    }

                    last_timestamp = recieved_msg.timestamp;

                }


                if(is_leader){

                    //Need to send a heartbeat then
                    if(!message_sent){
                        log_entry heartbeat = {latest_LSN, current_term, ""};
                        new_log_entry(heartbeat);
                    }
                }
                else {


                }

                // Sleep to prevent monopolization of runtime
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

            }

        }

        // 0 success 1 on failure
        int new_log_entry(log_entry new_log_entry) {

            // Attempt to tell all other entries about this update
            msg new_message = {new_log_entry, MessageType::NEWENTRY, node_id, std::chrono::steady_clock::now()};
            send_to_all_neighbors(new_message);

            // For heartbeats, exit here
            if(new_log_entry.entry_data.empty()){
                return 0;
            }

            // Allow all the threads to "respond"
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            // Count the number of responses given
            int num_acknowledgements = 0;
            for(msg & recieved_message : mailbox){
                if(recieved_message.msg_type == MessageType::NEW_ENTRY_ACK){
                    num_acknowledgements++;
                }
            }

            //Check to see if more than a majority has been reached
            if(num_acknowledgements > (neighbors.size() / 2)){

                // Commit the messages to all nodes
                msg new_message = {new_log_entry, MessageType::COMMIT, node_id, std::chrono::steady_clock::now()};
                send_to_all_neighbors(new_message);

            }
            else {
                //Return 1 to indicate that a majority concensus was not reached
                return 1;
            }

            return 0;
        }

        void run_election(){
            int vote_count = 1; //1 vote because the node votes for itself

            log_entry data;
            
            if(!log.empty()){
                data = log.back();
            }
            else {
                data = {latest_LSN, current_term, ""};
            }

            msg packet = {data, MessageType::REQUEST_VOTES, node_id, std::chrono::steady_clock::now()};

            send_to_all_neighbors(packet);

            // Allow all the threads to "respond"
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            for(msg & recieved_message : mailbox){
                if(recieved_message.msg_type == MessageType::ELECTION_RESPONSE){
                    vote_count++;
                }
            }
            
            // More than a majority have responded, we are now leader
            if(vote_count > (neighbors.size() / 2)){
                is_leader = true;

            }

        }

        bool request_vote_impl(log_entry & new_message){

            // Already voted this election cycle
            if(voted_for != -1){
                return false;
            }

            // Don't elect an out of date node
            if(new_message.term_idx < current_term){
                return false;
            }

            // Update current term
            if(new_message.term_idx > current_term){
                current_term = new_message.term_idx;
            }

            // Check if the candidates log is out of date
            if(!log.empty()){

                // Check if current index is greater than the candidates
                if(latest_LSN > new_message.index){
                    return false;
                }

                // Check if current term is later than the new message idx
                if(current_term > new_message.term_idx){
                    return false;
                }
            }

            // give the candidate the vote
            return true;

        }

        inline void send_to_all_neighbors(msg & new_message){
            for(auto * peer : neighbors){
                if(peer != this){
                    peer->recieve_message(new_message);
                }
            }
        }

        inline void recieve_message(msg recived_msg){
            std::lock_guard<std::mutex> lock(mailbox_mutex);
            mailbox.push_back(recived_msg);
        }

        node(int id) {
            node_id = id;
            alive = true;
            latest_LSN = 0;
            current_term = 0;
            leader_idx = -1; // no leader right now
            int random_offset = std::rand() % 10;
            random_offset *= 10;
            timeout = std::chrono::milliseconds(300 + random_offset);
        }

};