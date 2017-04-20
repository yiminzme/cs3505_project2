//Written by Josh Christensen

#include "socket_manager.h"
#include <boost/bind.hpp>
#include <functional>
#include "logger.h"

using namespace CS3505;
using namespace std;
using namespace boost::asio;

/*
 *
 */
void socket_manager::do_work()
{
  io_service::work work(our_io_service); //Create a work object.
  our_io_service.run();
}

/*
 * Generates a socket string name unique among the existing sockets.
 */
string socket_manager::generate_socket_name()
{
  return "0"; //Placeholder.
}

/*
 * The function called when we need to accept a new socket connection. Passed too boost's async_accept. 
 */
void socket_manager::accept_socket(socket_state *socket, const boost::system::error_code &error_code)
{
  logger *log = logger::get_logger();
  if (error_code)
    {
      log->log(string("Socket Accept Error: ") + error_code.message(), loglevel::ERROR);
    }
  else
    {
      //Start accepting sockets again.
      socket_state* ss = new socket_state(our_io_service, our_endpoint, buff_size);
      our_acceptor->async_accept(ss->socket, 
			     boost::bind(&socket_manager::accept_socket, this, ss, boost::asio::placeholders::error));  
      mtx.lock(); //Lock on accepting a socket. This way, we don't drop connections, but our map is threadsafe.
      //Add the socket to our map of sockets.
      string name = generate_socket_name();
      
      //Set the identifier, and put it in our hashmap
      socket->identifier = name;
      sockets->emplace(name, socket);

      //Start an async read some to receive data on the socket.
      socket->socket.async_read_some(buffer(socket->buffer, buff_size), 
				     boost::bind(&socket_manager::read_data, 
						 this, socket, boost::asio::placeholders::error, 
						 boost::asio::placeholders::bytes_transferred));
      mtx.unlock();
      //Log that we got a socket
      log->log(string("Connection Recieved: ") + name, loglevel::INFO); 
      //Inform our callback owner that we recieved a socket connection.
      callbacks.client_connected(name);
    }
}

/*
 * The function called when we need to handle data recieved on an incoming socket.
 */
void socket_manager::read_data(socket_state *socket_state, const boost::system::error_code &error_code, int bytes_read)
{
  //Grab the logger
  logger *log = logger::get_logger();
  //If we had an error, handle it.
  if (error_code)
    {
      //If the socket is disconnecting, handle the disconnect.
      if (error_code == error::eof)
	{
	  handle_disconnect(socket_state);
	}
      else
	{
	  log->log(string("Socket Recieve Error: ") + error_code.message(), loglevel::ERROR);
	}
    }
  else
    {
      //Start reading again
      socket_state->socket.async_read_some(buffer(socket_state->buffer, buff_size), 
					   boost::bind(&socket_manager::read_data, 
						       this, socket_state, boost::asio::placeholders::error, 
						       boost::asio::placeholders::bytes_transferred));
      log->log("Read Data Called", loglevel::INFO);
      //Lock on the socket, to ensure that we don't have race conditions with the stream.
      socket_state->mtx.lock();
      //Read bytes_read bytes into the socket buffer
      for (int i = 0; i <  bytes_read; i++)
	{
	  socket_state->stream << socket_state->buffer[i];
	}
      //Read through the stream, extracting messages
      string message;
      //Read until the failbit is set, indicating we didn't find a newline.
      while (!socket_state->stream.failbit)
	{
	  getline(socket_state->stream, message);
	  //If the fail bit isn't set, call the method to handle this message.
	  if (!socket_state->stream.failbit)
	    {
	      callbacks.message_received(socket_state->identifier, message);
	      //Log that we recieved a message.
	      log->log(string("Message Recieved From ") + 
		       socket_state->identifier + string(": ") + 
		       message, loglevel::INFO);
	    }
	}
      //Put whatever we got back into the stream, to be handled later.
      socket_state->stream << message;
  
      //Unlock on the socket
      socket_state->mtx.unlock();
    }
}

/*
 * Handles disconnecting a socket when the client disconnects.
 */
void socket_manager::handle_disconnect(socket_state *disconnected_socket)
{
  //Lock
  mtx.lock();
  //Remove the socket state from our map.
  sockets->erase(disconnected_socket->identifier);
  //Delete the socket.
  delete(disconnected_socket);
  //Unlock
  mtx.unlock();
}

/*
 * Description:
 * Creates a new socket_manager class. Before this class is constructed, a logging resource should be initialized.
 *
 * Parameters:
 * callbacks: The network_callbacks struct we should use for our callbacks.
 */
socket_manager::socket_manager(network_callbacks callbacks)
  : callbacks(callbacks)
{
  //Spawn a thread to do the async io work.
  work_thread = new thread(std::bind(&socket_manager::do_work, this));
  //Create the map for accepting sockets.
  sockets = new SOCKETMAP();
  //Set the port for the endpoint
  our_endpoint.port(PORT);
  //Create an acceptor to accpet connecting sockets
  our_acceptor = new ip::tcp::acceptor(our_io_service, our_endpoint);
  //Start accepting sockets.
  socket_state* ss = new socket_state(our_io_service, our_endpoint, buff_size);
  our_acceptor->async_accept(ss->socket, 
			     boost::bind(&socket_manager::accept_socket, this, ss, boost::asio::placeholders::error));
  //Log that we've started accepting sockets
  logger *log = logger::get_logger();
  log->log("Networking accepting sockets..", loglevel::INFO);
  
}

/*
 * Description: The destructor for the socket_manager class. Cleans up all used system resources.
 */
socket_manager::~socket_manager()
{
  //Clean up all our sockets.
  for(SOCKETMAP::iterator iterator = sockets->begin(); iterator != sockets->end(); iterator++)
    {
      delete(iterator->second);
    }
  //Stop all io.
  our_io_service.stop();
  //Join our work thread
  work_thread->join();
  //Clean up our endpoint
  
  //Delete our socket map.
  delete(sockets);
}

/*
 * Sends the specified message string to the specified client identifier. If that client identifier
 * corresponds to a currently connected client, sends the message and returns true. Otherwise,
 * returns false.
 */
bool socket_manager::send_message(string message, string client_identifier)
{

}

/*
 * Sends a message to all connected clients. Returns true if there's at least one client. false otherwise.
 */
bool socket_manager::send_all(string message)
{

}
