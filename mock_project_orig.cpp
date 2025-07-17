
#include <iostream>
#include <string>
#include <thread>
#include <boost/asio.hpp>
#include <openssl/ssl.h>
#include <boost/asio/ssl.hpp>

    //moved from main as it's hurting readability:
    //cant use 'using' as boost::asio::ssl is a namespace
    namespace ssl = boost::asio::ssl;
    using boost::asio::ip::tcp;
    




//added feature for time stamp
//doesn't have to be thread safe because its only called from one thread.
std::string time_stamp_func() {
    std::time_t now = std::time(nullptr);
    std::tm* local_tm = std::localtime(&now);  

    //format: "HH:MM:SS" + '\0'
    char buffer[9]; 
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", local_tm);
    return std::string(buffer);
}


void start_read_thread  (boost::asio::io_context& io_context,
                        ssl::stream<tcp::socket>& socket,
                        std::string& read_message,
                        std::atomic<bool>& connected){

                                                                                   //\n catch
    boost::asio::async_read_until(socket, boost::asio::dynamic_buffer(read_message), '\n',
    [&socket, &read_message, &connected, &io_context](boost::system::error_code ec, size_t length){
        if (!ec && connected){

            //READ THE MESSAGE!
            //parsed to variable 'message'
            //dont read the final /n added to the line
            std::string message(read_message.substr(0, length-1));

            //erase the entire read_message buffer
            read_message.erase(0, length);

            //print message to screen
            std::cout << message << std::endl;

            //recursively call start thread. look at while loop instead
            
            start_read_thread(io_context, socket, read_message, connected);
            
     

        }
        else{
            //disabling errors on shutdown
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (ec != boost::asio::error::operation_aborted &&
                ec != boost::asio::error::eof &&
                ec != boost::asio::error::not_connected &&
                ec.message() != "stream truncated") {
                std::cerr << "Error message: " << ec.message() << std::endl;
            }
            
            if (socket.lowest_layer().is_open()) {
                socket.lowest_layer().close();
            }
            connected = false;
        }

    });
}
void start_write_thread(ssl::stream<tcp::socket>& socket,
                        std::string& name,
                        std::atomic<bool>& connected){

    std::thread([&socket, name, &connected](){
        while (connected){

            std::string line;
            

            //error check input - stream input closed or in error state.
            if (!std::getline(std::cin, line)){
                connected = false;
                boost::system::error_code input_ec;

                if (socket.lowest_layer().is_open()) {
                    socket.lowest_layer().close();
                }
                break;
            }

            // Skip empty lines with ANSI escape codes
            if (line.empty()) {
                // move cursor up one line
                std::cout << "\033[A";

                // Clear the entire line
                std::cout << "\33[2K\r";
                
                // Return cursor to start of line
                std::cout << "\r";
                
                // Ensures previous doesn't sit in a buffer
                std::cout.flush();
                continue;
            }

            // the /quit command:
            if (line == "/quit") {

                //send disconnect message to other user
                std::string disconnect_msg = name + " has disconnected. Chat closed.\n";
                boost::system::error_code disconnect_ec;
                boost::asio::write(socket, boost::asio::buffer(disconnect_msg), disconnect_ec);


                std::cout << "Disconnecting from chat... Chat closed." << std::endl;
                connected = false;

                // ssl shutdown if open
                if (socket.lowest_layer().is_open()) {
                    boost::system::error_code shutdown_ec;
                    socket.shutdown(shutdown_ec);

                                    // Close socket if open
                    if (socket.lowest_layer().is_open()) {
                        boost::system::error_code close_ec;
                        socket.lowest_layer().close(close_ec);
                    }
                }
                break;
            }

            //add timestamp and name to start of message. add \n to end of message to catch for read_message
            std::string message = '[' + time_stamp_func() + ']' + name + ": " + line + '\n'; 
            boost::system::error_code ec;

            //when quitting, dont allow software to send another message.
            if (!connected) break;

            // adding time stamp and name to your own terminal.
            // move cursor up one line
            std::cout << "\033[A";

            // Clear the entire line
            std::cout << "\33[2K\r";
                
            // Return cursor to start of line
            std::cout << "\r";
            
            //send message to your own terminal with
            std::cout << message;

            // Ensures previous doesn't sit in a buffer
            std::cout.flush();


            //send message by socket!!
            boost::asio::write(socket, boost::asio::buffer(message), ec);

            //error checking message send
            if (ec){
                std::cerr << "Error message: " << ec.message() << std::endl;
                if (socket.lowest_layer().is_open()) {
                    socket.lowest_layer().close();
                }
                break;
            }
        }


    }).detach(); // thread now runs independently. start_write_thread runs in background.
}

//server function -> must async_accept and handshake as server
void start_accept_func (boost::asio::io_context& io_context,
                        ssl::context& ssl_context,
                        boost::asio::ip::tcp::acceptor& acceptor,
                        ssl::stream<tcp::socket>& socket,
                        std::string& name,
                        std::string& read_message,
                        std::atomic<bool>& connected){



    //async_accept used here instead of async_connect
    acceptor.async_accept([&](boost::system::error_code accept_ec, tcp::socket new_socket){

        if (!accept_ec) {

            //introducing new socket(before handshake)
            ssl::stream<tcp::socket> new_ssl_socket(std::move(new_socket), ssl_context);
            socket = std::move(new_ssl_socket);


            socket.async_handshake(ssl::stream_base::server,
            [&io_context, &acceptor, &socket, &name, &read_message, &connected](const boost::system::error_code& accept_ec) {
                if (!accept_ec){



                    std::cout << std::endl;
                    std::cout << "\033[1;32mHandshake attempt successful\033[0m" << std::endl; //green
                    std::cout << "\033[1;32mJoined the server.\033[0m" << std::endl; 
                    std::cout << std::endl;
                    connected = true;



                    start_read_thread(io_context, socket, read_message, connected);
                    start_write_thread(socket, name, connected);

                }
                else{
                    std::cout << std::endl;
                    std::cerr << "Error Message: " << accept_ec.message() << std::endl;
                    if (socket.lowest_layer().is_open()) {
                        socket.lowest_layer().close();
                    }
                }
            });
    
        }
        //this prevents error message if '/quit' is used before server creation
        else if (accept_ec != boost::asio::error::operation_aborted){
            std::cerr << "Error with accept: " << accept_ec.message() <<std::endl;
            if (socket.lowest_layer().is_open()) {
                socket.lowest_layer().close();
            }
        }
    
    });
}

//this function is used directly after user puts their details and port number in.
//client checks if it can join the server. if it can successfully join the server then it joins.
//if it can NOT join the server, then it creates a server. it then follows start_accept_func
//as a client function -> must async_connect and handshake as client
void connect_client_attempt(boost::asio::io_context& io_context, 
                            ssl::context& ssl_context,
                            ssl::stream<tcp::socket>& socket,
                            boost::asio::ip::tcp::acceptor& acceptor,
                            int port,
                            std::string& name,
                            std::string& read_message,
                            std::atomic<bool>& connected){



                                                            //from_string doesn't work
    //server endpoint created
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), port);

    using boost::system::error_code;

    //async_connect with endpoint just made        //more lambda expression                                                                      
    socket.lowest_layer().async_connect(endpoint, 
                                        [&io_context, &ssl_context, &socket, &acceptor, port, &name, &read_message, &connected, endpoint]
                                        (boost::system::error_code ec){



        //entire logic runs on whether async_connect works or is an error message.
        //if error message then create the server.
        if (!ec){

            //after async_connect attempt to handshake(as client)
            //wrapper layer is used for handshake so no need to use .lowest_layer()
            socket.async_handshake(ssl::stream_base::client,
            [&io_context, &ssl_context, &socket, &acceptor, port, &name, &read_message, &connected](const boost::system::error_code& ec) {
                if(!ec){
                        std::cout << std::endl;
                        std::cout << "\033[1;32mHandshake attempt successful\033[0m" << std::endl; //green
                        std::cout << "\033[1;32mJoined the server.\033[0m" << std::endl; 
                        std::cout << std::endl;
                    connected = true;

                    start_read_thread(io_context, socket, read_message, connected);
                    start_write_thread(socket, name, connected);
            }
                else{
                    std::cout << std::endl;
                    std::cout << "\033[1;31mHandshake attempt failed...\033[0m" << std::endl;
                    std::cout << "\033[1;31mClosing Application\033[0m" << std::endl;

                    if (socket.lowest_layer().is_open()) {
                        socket.lowest_layer().close();
                    }
                }
            });
        }
        else{
                std::cout << std::endl;
                std::cout << "\033[1;34mCreating server...\033[0m" << std::endl;

                boost::system::error_code ec;


                //open to ipv4
                acceptor.open(tcp::v4());


                //reuse address
                acceptor.set_option(boost::asio::socket_base::reuse_address(true));

                //bind acceptor to localhost w/ ipv4 and port of choosing
                //added endpoint to lambda capture to use the endpoint we already made
                acceptor.bind(endpoint);

                std::cout << "\033[1;34mServer Created.\033[0m" << std::endl;


                //open for listening
                acceptor.listen();
                std::cout << "\033[1;34mServer open for listening.\033[0m" << std::endl;




                //join
                start_accept_func(io_context, ssl_context, acceptor, socket, name, read_message, connected);

            }
        });
    }



int main(int argc, char* argv[]){

    int port;
    std::string name;

    //greeting menu:
    std::cout << std::endl;
    std::cout << "----------------------------------" << std::endl;
    std::cout << "Welcome to peer-to-peer messaging!" << std::endl;
    std::cout << std::endl;
    std::cout << "This chat room is encrypted using openssl" << std::endl;
    std::cout << "Below you will enter a user name and a port for communication." << std::endl;
    std::cout << std::endl;
    std::cout << "If you are the first person on the port a server will be created..." << std::endl;
    std::cout << "Otherwise you will join the pre-existing server!" << std::endl;
    std::cout << std::endl;
    std::cout << "Typing /quit in the chat room will exit the chat." << std::endl;
    std::cout << "Enjoy!" << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;

    if (argc == 1){

        std::cout << "Please enter your name: ";
        std::getline(std::cin, name);

        std::cout << "Please enter your desired port number[1024-65535]: ";
        std::cin >> port;
        std::cin.ignore();
    }
    else if (argc == 3){
        name = argv[1];
        port = std::stoi(argv[2]);
    }
    else{
        std::cout << "Incorrect usage" << std::endl;
        std::cout << "on startup try either...: " << std::endl;
        std::cout << "./mock_project" << std::endl;
        std::cout << "./mock_project [name] [port]" <<std::endl;
        return 0;
    }
try{
    //adding ssl context 
    ssl::context ssl_context(ssl::context::sslv23);

    // adding key and cert generated previously and added to cert file (still compiles!)
    ssl_context.use_certificate_chain_file("peer.crt");
    ssl_context.use_private_key_file("peer.key", ssl::context::pem);


    boost::asio::io_context io_context; //necessary

    //link new ssl socket io_context and ssl_context
    ssl::stream<tcp::socket> socket(io_context, ssl_context);

    //acceptor stays the same
    tcp::acceptor acceptor(io_context);
    std::string read_message;
    std::atomic<bool> connected{false}; //for threading



    //adding tasks to 'queue'. all exectuted by io_context.run() after
    connect_client_attempt(io_context, ssl_context, socket, acceptor, port, name, read_message, connected);

    //run!
    io_context.run();
}
catch (std::exception & e){
    std::string exception_msg = e.what();
    if (exception_msg != "close: Bad file descriptor [system:9]"){
       std::cerr << "Exception: " << exception_msg << std::endl;
    }
}
}