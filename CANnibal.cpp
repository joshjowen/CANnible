#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <mutex>
#include <atomic>
#include <queue>

#include <signal.h>
#include <getopt.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include <sys/ioctl.h>
#include <net/if.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <set>

#include <json/json.h>


// build deps: cmake libwebsocketpp-dev libboost-system-dev libjsoncpp-dev
// deps: libjsoncpp1 can-utils

std::vector<std::string> can_ports = {};
int ws_port = 8081;

int can_socket;

volatile std::atomic<bool> kill_main;

websocketpp::server<websocketpp::config::asio> ws_server;
std::set<websocketpp::connection_hdl,std::owner_less<websocketpp::connection_hdl>> ws_connections;

void* ws_loop(void*);

void ws_send(std::string message);

void wsMessageCB(websocketpp::server<websocketpp::config::asio>* s,
                 websocketpp::connection_hdl hdl,
                 websocketpp::server<websocketpp::config::asio>::message_ptr msg);

bool open_can_socket(void);

void print_usage(std::string prog_name);

void sigintHandler(int signal);


int main(int argc, char* argv[])
{
  kill_main = false;
  long int raw_port;

  if(signal(SIGINT, sigintHandler) == SIG_ERR)
  {
    std::cerr << "Could not install signal handler" << std::endl;
    return -1;
  }

  int opt_return;

  while ((opt_return = getopt(argc, argv, "p:n:h?")) != -1)
  {
    switch (opt_return)
    {
      case 'p':
        char *remain;
        errno = 0;
        raw_port = strtol(optarg, &remain, 10);

        if ((errno != 0) || (*remain != '\0') || (raw_port < 0)
                                            || (raw_port > INT_MAX))
        {
          std::cout << "invalid websocket port provided:" << raw_port
                                                          << std::endl;
          return -1;

        }
        ws_port = raw_port;
        break;
      case 'h':
      case '?':
      default:
        print_usage(argv[0]);
        return -1;
    }
  }

  if (!open_can_socket())
  {
    return -1;
  }

  pthread_t ws_thread;

  if (ws_port <= 0)
  {
    std::cout << "No port provided for websocket" << std::endl;
    print_usage(argv[0]);
    return -1;
  }

  if (pthread_create(&ws_thread, NULL, ws_loop, NULL))
  {
    std::cout << "Unable to start websocket thread" << std::endl;
    kill_main = true;
  }

  std::string can_string;
  Json::FastWriter fastWriter;
  
  while (!kill_main)
  {
    struct can_frame frame_rd;
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(can_socket, &rset);

    struct timeval timeout = {0, 500};

    if (select((can_socket + 1), &rset, NULL, NULL, &timeout) > 0)
    {
      struct sockaddr_can addr;
      struct ifreq ifr;
      socklen_t len = sizeof(addr);
      int recvbytes;

      if (recvbytes = recvfrom(can_socket, &frame_rd, sizeof(struct can_frame), 0, (struct sockaddr*)&addr, &len)
                                                                                == sizeof(struct can_frame))
      {
        ifr.ifr_ifindex = addr.can_ifindex;
        ioctl(can_socket, SIOCGIFNAME, &ifr);

        for (int n = 0; n < can_ports.size(); n++)
        {
          Json::Value sendJson;
          if (strcmp(can_ports[n].c_str(), ifr.ifr_name) == 0)
          {
            sendJson["port"] = std::string(ifr.ifr_name);
            
            std::ostringstream can_id;
            int width = 3;
            int ican_id = (frame_rd.can_id & ~CAN_EFF_FLAG);

            if (ican_id > 0x7FF)
            {
              width = 8;
            }
            
            can_id << std::setfill('0') << std::setw(width) << std::hex << ican_id;
            sendJson["id"] = can_id.str();

            std::ostringstream can_data;
            for (int i = 0; i < frame_rd.can_dlc; i++)
            {
              if (i > 0)
              {
                can_data << " ";
              }
              can_data << std::setfill('0') << std::setw(2) << std::hex << (int)frame_rd.data[i];
            }
            sendJson["data"] = can_data.str();

            can_string = fastWriter.write(sendJson);

            ws_send(can_string);

            break;
          }
        }
      }
    }
  }

  close(can_socket);
  ws_server.stop();

  pthread_join(ws_thread, NULL);
  return 0;
}


void on_open_ws(websocketpp::connection_hdl hdl)
{
    ws_connections.insert(hdl);
}


void on_close_ws(websocketpp::connection_hdl hdl)
{
    ws_connections.erase(hdl);
}


void* ws_loop(void*)
{
  try
  {
    ws_server.set_access_channels(websocketpp::log::alevel::none);
    ws_server.clear_access_channels(websocketpp::log::alevel::none);
    ws_server.init_asio();
    ws_server.set_message_handler(
      websocketpp::lib::bind(&wsMessageCB, &ws_server,
                             websocketpp::lib::placeholders::_1,
                             websocketpp::lib::placeholders::_2));
    ws_server.set_open_handler(&on_open_ws);
    ws_server.set_close_handler(&on_close_ws);
    ws_server.listen(ws_port);
    ws_server.start_accept();

    std::cout << "Websocket available on port " << ws_port << std::endl;
    ws_server.run();
  }
  catch (websocketpp::exception const & e)
  {
    std::cout << e.what() << std::endl;
    kill_main = true;
  }

  pthread_exit(NULL);
}


void ws_send(std::string message)
{
  websocketpp::lib::error_code ec;
  std::set<websocketpp::connection_hdl,std::owner_less<websocketpp::connection_hdl> >::iterator it;
  for (it = ws_connections.begin(); it != ws_connections.end(); ++it)
  {
    ws_server.send(*it, message, websocketpp::frame::opcode::text);
  }
}


void wsMessageCB(websocketpp::server<websocketpp::config::asio>* serv,
                 websocketpp::connection_hdl hdl,
                 websocketpp::server<websocketpp::config::asio>::message_ptr msg)
{
  hdl.lock().get();
  std::string response;
  Json::Value recievedJson;
  Json::Reader reader;
  Json::Value responseJson;

  if (reader.parse(msg->get_payload().c_str(), recievedJson))
  {
    std::string can_port = recievedJson.get("can_port", "").asString();

    if (!can_port.empty())
    {
      can_ports.clear();
      can_ports.push_back(can_port);
      responseJson["success"] = true;
      responseJson["message"] = "can_ports set to " + can_port;
    }
    else
    {
      responseJson["success"] = false;
      responseJson["message"] = "Invalid can_port provided";
    }
  }
  else
  {
    responseJson["success"] = false;
    responseJson["message"] = reader.getFormattedErrorMessages();
  }

  Json::FastWriter fastWriter;
  response = fastWriter.write(responseJson);

  try
  {
      serv->send(hdl, response, msg->get_opcode());
  }
  catch (websocketpp::exception const & e)
  {
    std::cerr << "Failed to respond to websocket client." << std::endl
              << e.what() << std::endl;
  }

  return;
}


bool open_can_socket(void)
{
  can_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (can_socket < 0)
  {
    std::cerr << "Failed to open a CAN socket!" << std::endl;
    return false;
  }

  struct sockaddr_can addr;
  addr.can_family = AF_CAN;
  addr.can_ifindex = 0;
  if (bind(can_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0)
  {
    std::cerr << "Failed to bind to CAN socket" << std::endl;
    return false;
  }
  return true;
}


void print_usage(std::string prog_name)
{
    std::cout << std::endl << "usage: " << prog_name << " [options]"
      << std::endl << std::endl << "options:"
      << std::endl << "\t-p {port}   - websocket server port (default: 8081)"
      << std::endl << std::endl;
}


void sigintHandler(int signal)
{
  kill_main = true;
}
