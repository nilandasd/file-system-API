// $Id: cix.cpp,v 1.10 2020-07-18 23:33:51-07 - - $

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"


logstream outlog (cout);
struct cxi_exit: public exception {};

unordered_map<string,cxi_command> command_map {
   {"exit", cxi_command::EXIT},
   {"help", cxi_command::HELP},
   {"ls"  , cxi_command::LS  },
   {"put" , cxi_command::PUT },
};

static const char help[] = R"||(
exit         - Exit the program.  Equivalent to EOF.
get filename - Copy remote file to local host.
help         - Print help summary.
ls           - List names of files on remote server.
put filename - Copy local file to remote host.
rm filename  - Remove file from remote server.
)||";


vector<string> split(string& line){
   string word = "";
   vector<string> words;
   bool flag = false;
   for(size_t i=0; i<line.size(); ++i){
     if(line[i]==' '){
        if(flag==true){
           words.push_back(word);
           word = "";
           flag = false;
        }
     }else{
       flag=true;
       word+=line[i];
     }
  }
  if(word.size()!=0) words.push_back(word);
  return words;
}


void cxi_help() {
   cout << help;
}

void cxi_ls (client_socket& server) {
   cxi_header header;
   header.command = cxi_command::LS;
   outlog << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   outlog << "received header " << header << endl;
   if (header.command != cxi_command::LSOUT) {
      outlog << "sent LS, server did not return LSOUT" << endl;
      outlog << "server returned " << header << endl;
   }else{
      size_t host_nbytes = ntohl (header.nbytes);
      auto buffer = make_unique<char[]> (host_nbytes + 1);
      recv_packet (server, buffer.get(), host_nbytes);
      outlog << "received " << host_nbytes << " bytes" << endl;
      outlog << "LS output: " << endl;
      buffer[host_nbytes] = '\0';
      cout << buffer.get();
      cout << endl;
   }
}

void cxi_put (client_socket& server, const string stringfile) {
   cxi_header header;
   strcpy(header.filename,stringfile.c_str());
   ifstream ifs(header.filename, ios_base::binary);
   if (not ifs) {
      outlog << "PUT ERROR: " << strerror (errno) << endl;
      return;
   }
   ifs.seekg (0, ifs.end);
   size_t filesize = ifs.tellg();
   char * buffer = new char [filesize];
   ifs.seekg (0, ifs.beg);
   ifs.read (buffer,filesize);
   if (ifs) {
      outlog << "read " << filesize << " bytes"
             << endl;
   }else{
      outlog << "PUT ERROR: only " << ifs.gcount()
             << "bytes could be read" << endl;
      outlog << strerror(errno) << endl;
      ifs.close();
      delete[] buffer;
      return;
   }
   ifs.close();
   header.command = cxi_command::PUT;
   header.nbytes = htonl(filesize);
   outlog << "sending header: " << header << endl;
   send_packet (server, &header, sizeof header);
   send_packet (server, buffer, filesize);
   outlog << "sending " << filesize << " bytes" << endl;
   recv_packet (server, &header, sizeof header);
   outlog << "header recieved: "<< header << endl;
   if (header.command != cxi_command::ACK) {
     outlog << "PUT_ERROR: server did not return ACK"
            << endl;
     outlog << strerror (ntohl(header.nbytes)) << endl;

   }
   delete[] buffer;
}

void cxi_get (client_socket& server, const string stringfile) {
   cxi_header header;
   strcpy(header.filename,stringfile.c_str());
   header.command = cxi_command::GET;
   header.nbytes =0;
   outlog << "sending header: " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   outlog << "header recieved: "<< header << endl;
   if (header.command != cxi_command::FILEOUT) {
      outlog << "GET ERROR: "
             << strerror (ntohl(header.nbytes)) << endl;
   }else{
      size_t filesize = ntohl(header.nbytes);
      char * buffer = new char [filesize];
      recv_packet (server, buffer, filesize);
      outlog << "recieved "<< filesize << " bytes" << endl;
      ofstream ofs(header.filename, ios_base::binary);
      ofs.write(buffer,filesize);
      if (not ofs) {
         outlog << "GET ERROR: " << strerror (errno) << endl;
      }else{
        outlog << "wrote " << filesize << " bytes" << endl;
      }
      ofs.close();
      delete[] buffer;
   }
}

void cxi_rm(client_socket& server, const string stringfile){
  cxi_header header;
  strcpy(header.filename,stringfile.c_str());
  header.command = cxi_command::RM;
  header.nbytes =0;
  outlog << "sending header: " << header << endl;
  send_packet (server, &header, sizeof header);
  recv_packet (server, &header, sizeof header);
  outlog << "recieved header: " << header << endl;
  if (header.command != cxi_command::ACK) {
     outlog << "RM ERROR: "
            << strerror (ntohl(header.nbytes)) << endl;
  }
}

void usage() {
   cerr << "Usage: " << outlog.execname() << " [host] [port]" << endl;
   throw cxi_exit();
}

int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   outlog << "starting" << endl;
   vector<string> args (&argv[1], &argv[argc]);
   if (args.size() != 2) usage();
   string host = get_cxi_server_host (args, 0);
   in_port_t port = get_cxi_server_port (args, 1);
   outlog << to_string (hostinfo()) << endl;
   try {
      outlog << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      outlog << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         outlog << "====================================" << endl;
         outlog << "";
         getline (cin, line);
         if (cin.eof()) throw cxi_exit();
         vector<string> words = split(line);
         if(words.size()== 0) continue;
         if(words[0]=="ls"){
           if(words.size()!=1){
             outlog << "LS usage: ls" << endl;
             continue;
           }
           cxi_ls (server);
         }else if(words[0]=="help"){
           if(words.size()!=1){
             outlog << "HELP usage: help" << endl;
             continue;
           }
           cxi_help();
         }else if(words[0]=="exit"){
            if(words.size()!=1){
              outlog << "EXIT usage: exit" << endl;
              continue;
            }
           throw cxi_exit();
         }else if(words[0]=="put"){
            if(words.size()!=2){
              outlog << "PUT usage: put {filename}" << endl;
              continue;
            }
           cxi_put(server, words[1]);
         }else if(words[0]=="get"){
            if(words.size()!=2){
              outlog << "GET usage: get {filename}" << endl;
              continue;
            }
           cxi_get(server, words[1]);
         }else if(words[0]=="rm"){
            if(words.size()!=2){
                outlog << "RM usage: rm {filename}" << endl;
                continue;
              }
             cxi_rm(server, words[1]);
         }else{
           outlog << words[0] << ": invalid command" << endl;
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
