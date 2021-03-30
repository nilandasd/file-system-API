// $Id: protocol.cpp,v 1.12 2020-07-18 23:33:51-07 - - $

#include <string>
#include <unordered_map>
using namespace std;

#include "protocol.h"

string to_string (cxi_command command) {
   switch (command) {
      case cxi_command::ERROR  : return "ERROR"  ;
      case cxi_command::EXIT   : return "EXIT"   ;
      case cxi_command::GET    : return "GET"    ;
      case cxi_command::HELP   : return "HELP"   ;
      case cxi_command::LS     : return "LS"     ;
      case cxi_command::PUT    : return "PUT"    ;
      case cxi_command::RM     : return "RM"     ;
      case cxi_command::FILEOUT: return "FILEOUT";
      case cxi_command::LSOUT  : return "LSOUT"  ;
      case cxi_command::ACK    : return "ACK"    ;
      case cxi_command::NAK    : return "NAK"    ;
      default                  : return "????"   ;
   };
}


void send_packet (base_socket& socket,
                  const void* buffer, size_t bufsize) {
   const char* bufptr = static_cast<const char*> (buffer);
   ssize_t ntosend = bufsize;
   do {
      ssize_t nbytes = socket.send (bufptr, ntosend);
      if (nbytes < 0) throw socket_sys_error (to_string (socket));
      bufptr += nbytes;
      ntosend -= nbytes;
   }while (ntosend > 0);
}

void recv_packet (base_socket& socket, void* buffer, size_t bufsize) {
   char* bufptr = static_cast<char*> (buffer);
   ssize_t ntorecv = bufsize;
   do {
      ssize_t nbytes = socket.recv (bufptr, ntorecv);
      if (nbytes < 0) throw socket_sys_error (to_string (socket));
      if (nbytes == 0) throw socket_error (to_string (socket)
                                           + " is closed");
      bufptr += nbytes;
      ntorecv -= nbytes;
   }while (ntorecv > 0);
}


ostream& operator<< (ostream& out, const cxi_header& header) {
   string code = to_string (header.command);
   cout << "{" << ntohl (header.nbytes) << ","
        << unsigned (header.command)
        << "(" << code << "),\"" << header.filename << "\"}";
   return out;
}

string get_cxi_server_host (const vector<string>& args, size_t index) {
   if (index < args.size()) return args[index];
   char* host = getenv ("CIX_SERVER_HOST");
   if (host != nullptr) return host;
   return "localhost";
}

in_port_t get_cxi_server_port (const vector<string>& args,
                               size_t index) {
   string port = "-1";
   if (index < args.size()) port = args[index];
   else {
      char* envport = getenv ("CIX_SERVER_PORT");
      if (envport != nullptr) port = envport;
   }
   cout<<"port: "<<port<<endl;
   return stoi (port);
}
