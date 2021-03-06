// $Id: cixd.cpp,v 1.9 2020-07-18 23:33:51-07 - - $

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"

logstream outlog (cout);
struct cxi_exit: public exception {};

void reply_ls (accepted_socket& client_sock, cxi_header& header) {
   const char* ls_cmd = "ls -l 2>&1";
   FILE* ls_pipe = popen (ls_cmd, "r");
   if (ls_pipe == NULL) {
      outlog << "ls -l: popen failed: " << strerror (errno) << endl;
      header.command = cxi_command::NAK;
      header.nbytes = htonl (errno);
      send_packet (client_sock, &header, sizeof header);
      return;
   }
   string ls_output;
   char buffer[0x1000];
   for (;;) {
      char* rc = fgets (buffer, sizeof buffer, ls_pipe);
      if (rc == nullptr) break;
      ls_output.append (buffer);
   }
   int status = pclose (ls_pipe);
   if (status < 0) outlog << ls_cmd << ": " << strerror (errno) << endl;
              else outlog << ls_cmd << ": exit " << (status >> 8)
                          << " signal " << (status & 0x7F)
                          << " core " << (status >> 7 & 1) << endl;
   header.command = cxi_command::LSOUT;
   header.nbytes = htonl (ls_output.size());
   memset (header.filename, 0, FILENAME_SIZE);
   outlog << "sending header: " << header << endl;
   send_packet (client_sock, &header, sizeof header);
   send_packet (client_sock, ls_output.c_str(), ls_output.size());
   outlog << "sent " << ls_output.size() << " bytes" << endl;
}

void reply_put (accepted_socket& client_sock, cxi_header& header){
  size_t filesize = ntohl(header.nbytes);
  char * buffer = new char [filesize];
  ofstream ofs(header.filename, ios_base::binary);
  recv_packet (client_sock, buffer, filesize);
  outlog << "recieved " << filesize << " bytes" << endl;
  ofs.write(buffer,filesize);
  if (not ofs) {
     outlog << "REPLY_PUT ERROR: of.write(): "
            << strerror (errno) << endl;
     header.command = cxi_command::NAK;
     header.nbytes = htonl (errno);
     memset (header.filename, 0, FILENAME_SIZE);
     send_packet (client_sock, &header, sizeof header);
     ofs.close();
     delete[] buffer;
     return;
  }
  outlog << "wrote " << filesize << " bytes" << endl;
  ofs.close();
  header.command = cxi_command::ACK;
  header.nbytes = 0;
  memset (header.filename, 0, FILENAME_SIZE);
  outlog << "sending header: " << header << endl;
  send_packet (client_sock, &header, sizeof header);
  delete[] buffer;
}

void reply_get (accepted_socket& client_sock, cxi_header& header){
  ifstream ifs(header.filename, ios_base::binary);
  if (not ifs) {
     outlog << "GET_ERROR: " << strerror (errno) << endl;
     header.command = cxi_command::NAK;
     header.nbytes = htonl (errno);
     outlog << "sending header: " << header << endl;
     send_packet (client_sock, &header, sizeof header);
     return;
  }
  ifs.seekg (0, ifs.end);
  size_t filesize = ifs.tellg();
  char * buffer = new char [filesize];
  ifs.seekg (0, ifs.beg);
  ifs.read (buffer,filesize);
  if (ifs) {
     outlog << "read " << filesize << " bytes" << endl;
  }else{
     outlog << "GET_ERROR: only " << ifs.gcount()
            << "bytes could be read" << endl;
     outlog << strerror (errno) << endl;
     header.command = cxi_command::NAK;
     header.nbytes = htonl (errno);
     memset (header.filename, 0, FILENAME_SIZE);
     send_packet (client_sock, &header, sizeof header);
     ifs.close();
     delete[] buffer;
     return;
  }
  ifs.close();
  header.command = cxi_command::FILEOUT;
  header.nbytes = htonl(filesize);
  outlog << "sending header: " << header << endl;
  send_packet (client_sock, &header, sizeof header);
  outlog << "sending " << filesize << " bytes" << endl;
  send_packet (client_sock, buffer, filesize);
  delete[] buffer;
}

void reply_rm (accepted_socket& client_sock, cxi_header& header){
  int i = unlink(header.filename);
  if(i!=0){
    header.command = cxi_command::NAK;
    header.nbytes = htonl (errno);
    memset (header.filename, 0, FILENAME_SIZE);
    outlog <<"RM ERROR: " << strerror (errno) << endl;
    outlog << "sending header: " << header << endl;
    send_packet (client_sock, &header, sizeof header);
  }else{
    outlog << "file deleted" << endl;
    header.command = cxi_command::ACK;
    header.nbytes = 0;
    memset (header.filename, 0, FILENAME_SIZE);
    outlog << "sending header: " << header << endl;
    send_packet (client_sock, &header, sizeof header);
  }
}

void run_server (accepted_socket& client_sock) {
   outlog.execname (outlog.execname() + "-server");
   outlog << "connected to " << to_string (client_sock) << endl;
   try {
      for (;;) {
         cxi_header header;
         outlog<<"===================================="<<endl;
         outlog<<"waiting to recieve header..."<<endl;
         recv_packet (client_sock, &header, sizeof header);
         outlog << "received header: " << header << endl;
         switch (header.command) {
            case cxi_command::LS:
               reply_ls (client_sock, header);
               break;
            case cxi_command::PUT:
               reply_put (client_sock, header);
               break;
            case cxi_command::GET:
               reply_get (client_sock, header);
               break;
            case cxi_command::RM:
               reply_rm (client_sock, header);
               break;
            default:
               outlog << "invalid client header:" << header << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      outlog << "caught cxi_exit" << endl;
   }
   outlog << "finishing" << endl;
   throw cxi_exit();
}

void fork_cxiserver (server_socket& server, accepted_socket& accept) {
   pid_t pid = fork();
   if (pid == 0) { // child
      server.close();
      run_server (accept);
      throw cxi_exit();
   }else {
      accept.close();
      if (pid < 0) {
         outlog << "fork failed: " << strerror (errno) << endl;
      }else {
         outlog << "forked cxiserver pid " << pid << endl;
      }
   }
}


void reap_zombies() {
   for (;;) {
      int status;
      pid_t child = waitpid (-1, &status, WNOHANG);
      if (child <= 0) break;
      outlog << "child " << child
             << " exit " << (status >> 8)
             << " signal " << (status & 0x7F)
             << " core " << (status >> 7 & 1) << endl;
   }
}

void signal_handler (int signal) {
   outlog << "signal_handler: caught " << strsignal (signal) << endl;
   reap_zombies();
}

void signal_action (int signal, void (*handler) (int)) {
   struct sigaction action;
   action.sa_handler = handler;
   sigfillset (&action.sa_mask);
   action.sa_flags = 0;
   int rc = sigaction (signal, &action, nullptr);
   if (rc < 0) outlog << "sigaction " << strsignal (signal)
                      << " failed: " << strerror (errno) << endl;
}


int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   outlog << "starting" << endl;
   if(argc!=2){
     outlog<<"Usage: cixd {port}"<<endl;
     return EXIT_FAILURE;
  }
   vector<string> args (&argv[1], &argv[argc]);
   signal_action (SIGCHLD, signal_handler);
   cout<<"args: "<<args[0]<<endl;
   in_port_t port = get_cxi_server_port (args, 0);
   try {
      server_socket listener (port);
      for (;;) {
         outlog << to_string (hostinfo()) << " accepting port "
             << to_string (port) << endl;
         accepted_socket client_sock;
         for (;;) {
            try {
               listener.accept (client_sock);
               break;
            }catch (socket_sys_error& error) {
               switch (error.sys_errno) {
                  case EINTR:
                     outlog << "listener.accept caught "
                         << strerror (EINTR) << endl;
                     break;
                  default:
                     throw;
               }
            }
         }
         outlog << "accepted " << to_string (client_sock) << endl;
         try {
            fork_cxiserver (listener, client_sock);
            reap_zombies();
         }catch (socket_error& error) {
            outlog << error.what() << endl;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      outlog << "caught cxi_exit" << endl;
   }
   outlog << "finishing" << endl;
   return 0;
}
