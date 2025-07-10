#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <openssl/ssl.h>
#include <boost/asio/ssl.hpp>

//lambda expressions everywhere! [captured variables](parameters){function body}

//pass by reference is very important for socket use. many variables which must be constantly updated

//entire basis on socket communication logic is: run if not error code

void start_read_thread  (boost::asio::io_context& io_context,
                        boost::asio::ip::tcp::socket& socket,
                        std::string& read_message,
                        std::atomic<bool>& connected){

    boost::asio::async_read_until(socket, boost::asio::dynamic_buffer(read_message), '\n',
    [&socket, &read_message, &connected, &io_context](boost::system::error_code ec, size_t length){
        if (!ec){

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
            std::cerr << "Error message: " << ec.message() << std::endl;
            socket.close();
            connected = false;
        }

    });
}
void start_write_thread(boost::asio::ip::tcp::socket& socket,
                        std::string name,
                        std::atomic<bool>& connected){
    std::thread([&socket, name, &connected](){
        while (connected){
            std::string line;

            //error check input
            if (!std::getline(std::cin, line)){
                connected = false;
                boost::system::error_code input_ec;
                socket.close(input_ec);
                break;
            }
                //add name to start of message. add \n to end of message to catch for read_message
            std::string message = name + ": " + line + '\n'; 
            boost::system::error_code ec;

            //send message by socket!!
            boost::asio::write(socket, boost::asio::buffer(message), ec);
            if (ec){
                std::cerr << "Error message: " << ec.message() << std::endl;
                connected = false;
                socket.close();
                break;
            }
        }


    }).detach(); // thread now runs independently. start_write_thread runs in background.
}

void start_accept_func (boost::asio::io_context& io_context,
                        boost::asio::ip::tcp::acceptor& acceptor,
                        boost::asio::ip::tcp::socket& socket,
                        std::string name,
                        std::string& read_message,
                        std::atomic<bool>& connected){

    using boost::asio::ip::tcp;

    //async_accept used here instead of async_connect
    acceptor.async_accept([&io_context, &acceptor, &socket, name, &read_message, &connected](boost::system::error_code ec, tcp::socket new_socket){
        if (!ec){
            std::cout << "Server created and joined!" << std::endl;

            //IF NEW_SOCKET ISNT USED ERROR_MESSAGE: END OF FILE
            socket = std::move(new_socket);
            connected = true;


            start_read_thread(io_context, socket, read_message, connected);
            start_write_thread(socket, name, connected);

        }
        else{
            std::cerr << "Error Message: " << ec.message() << std::endl;
        }
    });
}

//this function is used directly after user puts their details and port number in.
//client checks if it can join the server. if it can successfully join the server then it joins.
//if it can NOT join the server, then it creates a server. it then follows start_accept_func
void connect_client_attempt(boost::asio::io_context& io_context, 
                            boost::asio::ip::tcp::socket& socket,
                            boost::asio::ip::tcp::acceptor& acceptor,
                            int port,
                            std::string name,
                            std::string& read_message,
                            std::atomic<bool>& connected){
    using boost::asio::ip::tcp;



                                                            //from_string doesn't work
    //server endpoint created
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), port);

    //async_connect with endpoint just made        //more lambda expression
    socket.async_connect(endpoint, [&io_context, &socket, &acceptor, port, name, &read_message, &connected](boost::system::error_code ec){

        //entire logic runs on whether async_connect works or is an error message.
        //if error message then create the server.
        if (!ec){
            std::cout << "Successfully joined the server!" << std::endl;
            connected = true;

            start_read_thread(io_context, socket, read_message, connected);
            start_write_thread(socket, name, connected);
        }
        else{
            std::cout << "Creating server..." <<std::endl;

            //open to ipv4
            acceptor.open(tcp::v4());

            //reuse address, SOMETIMES DOESN'T ALLOW TO REUSE PORT IF NOT HERE:
            acceptor.set_option(boost::asio::socket_base::reuse_address(true));

            //bind acceptor to localhost w/ ipv4 and port of choosing
            acceptor.bind(tcp::endpoint(tcp::v4(), port));

            //open for listening
            acceptor.listen();

            //join
            start_accept_func(io_context, acceptor, socket, name, read_message, connected);

        }

    });
}


int main(int argc, char* argv[]){

    using boost::asio::ip::tcp;

    int port;
    std::string name;

    std::cout << "Welcome to peer-to-peer messaging!" << std::endl;

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
    boost::asio::io_context io_context; //necessary
    tcp::socket socket(io_context);
    tcp::acceptor acceptor(io_context);
    std::string read_message;
    std::atomic<bool> connected{false}; //for threading


    //adding tasks to 'queue'. all exectuted by io_context.run() after
    connect_client_attempt(io_context, socket, acceptor, port, name, read_message, connected);

    //run!
    io_context.run();
}
catch (std::exception & e){
    std::cerr << "Exception: " << e.what() << std::endl;
}
}