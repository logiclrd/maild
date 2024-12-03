#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <iostream>
#include <fstream>
#include <string>

#include <cstring>
#include <cstdio>

using namespace std;

int read_line(int sockfd, char *&buffer, int &buffer_length)
{
  int buffer_offset = 0;

  while (true)
  {
    if (buffer_offset >= buffer_length)
    {
      int new_buffer_length = (buffer_length + 1) * 2;
      char *new_buffer = new char[new_buffer_length];

      if (buffer)
      {
        memcpy(new_buffer, buffer, buffer_length);
        delete[] buffer;
      }

      buffer = new_buffer;
      buffer_length = new_buffer_length;
    }

    int num_recved = recv(sockfd, &buffer[buffer_offset], 1, 0);

    if (num_recved <= 0)
      return buffer_offset;

    if ((buffer_offset > 0)
     && (buffer[buffer_offset - 1] == '\r')
     && (buffer[buffer_offset] == '\n'))
      return buffer_offset - 1;

    buffer_offset++;
  }
}

namespace ClientState
{
  enum Type
  {
    Initial,
    Introduced,
    HalfEnvelope,
    FullEnvelope,
    Data,
  };
}

bool send_fully(int sockfd, char *buffer, int count)
{
  while (count > 0)
  {
    int num_sent = send(sockfd, buffer, count, 0);

    if (num_sent <= 0)
      return false;

    buffer += num_sent;
    count -= num_sent;
  }

  return true;
}

bool send_fully(int sockfd, char *message)
{
  return send_fully(sockfd, message, strlen(message));
}

struct Line
{
  char *Buffer;
  int Length;

  bool IsCommand(char *prefix)
  {
    int prefix_length = strlen(prefix);

    if (prefix_length > Length)
      return false;

    if (strncasecmp(Buffer, prefix, prefix_length) != 0)
      return false;

    return (prefix_length == Length)
        || (Buffer[prefix_length] == 0)
        || (Buffer[prefix_length] == 32);
  }

  bool operator ==(char *str)
  {
    int str_length = strlen(str);

    if (str_length != Length)
      return false;

    return (memcmp(Buffer, str, str_length) == 0);
  }

  bool operator !=(char *str)
  {
    return !(*this == str);
  }
};

ostream &operator <<(ostream &stream, const Line &line)
{
  string str(line.Buffer, line.Length);

  return stream << str;
}

void handle_client(int sockfd)
{
  char *buffer = NULL;
  int buffer_length = 0;
  int line_length;

  printf("Client task starting up\n");
  fflush(stdout);

  ClientState::Type state = ClientState::Initial;

  char spool_name[100];

  sprintf(spool_name, "/root/maild/spool/%d", getpid());

  ofstream spool(spool_name, ios::app);

  printf("Opened spool %s\n", spool_name);
  fflush(stdout);

  time_t current_time = time(NULL);

  spool << "--------------------------" << endl
        << "Session Start at " << ctime(&current_time) << endl;

  printf("Saying hello to the client\n");
  fflush(stdout);

  send_fully(sockfd, "220 Ready\r\n");

  while (true)
  {
    line_length = read_line(sockfd, buffer, buffer_length);

    Line line = { buffer, line_length };

    spool << "<< " << line << endl;

    switch (state)
    {
      case ClientState::Initial:
        if (line.IsCommand("HELO"))
        {
          spool << ">> 250 Ready" << endl;
          send_fully(sockfd, "250 Ready\r\n");
          state = ClientState::Introduced;
        }
        else if (line.IsCommand("QUIT"))
        {
          spool << ">> 250 Goodbye" << endl;
          send_fully(sockfd, "250 Goodbye\r\n");
          return;
        }
        else
        {
          spool << ">> 502 Unimplemented or unrecognized" << endl;
          send_fully(sockfd, "502 Unimplemented or unrecognized\r\n");
        }
        break;
      case ClientState::Introduced:
        if (line.IsCommand("RSET"))
        {
          spool << ">> 250 Flushed" << endl;
          send_fully(sockfd, "250 Flushed\r\n");
        }
        else if (line.IsCommand("QUIT"))
        {
          spool << ">> 250 Goodbye" << endl;
          send_fully(sockfd, "250 Goodbye\r\n");
          return;
        }
        else if (line.IsCommand("MAIL"))
        {
          spool << ">> 250 Got it" << endl;
          send_fully(sockfd, "250 Got it\r\n");
          state = ClientState::HalfEnvelope;
        }
        else
        {
          spool << ">> 502 Unimplemented or unrecognized" << endl;
          send_fully(sockfd, "502 Unimplemented or unrecognized\r\n");
        }
        break;
      case ClientState::HalfEnvelope:
        if (line.IsCommand("RSET"))
        {
          spool << ">> 250 Flushed" << endl;
          send_fully(sockfd, "250 Flushed\r\n");
          state = ClientState::Introduced;
        }
        else if (line.IsCommand("QUIT"))
        {
          spool << ">> 250 Goodbye" << endl;
          send_fully(sockfd, "250 Goodbye\r\n");
          return;
        }
        else if (line.IsCommand("RCPT"))
        {
          spool << ">> 250 Got it" << endl;
          send_fully(sockfd, "250 Got it\r\n");
          state = ClientState::FullEnvelope;
        }
        else
        {
          spool << ">> 502 Unimplemented or unrecognized" << endl;
          send_fully(sockfd, "502 Unimplemented or unrecognized\r\n");
        }
        break;
      case ClientState::FullEnvelope:
        if (line.IsCommand("RSET"))
        {
          spool << ">> 250 Flushed" << endl;
          send_fully(sockfd, "250 Flushed\r\n");
          state = ClientState::Introduced;
        }
        else if (line.IsCommand("QUIT"))
        {
          spool << ">> 250 Goodbye" << endl;
          send_fully(sockfd, "250 Goodbye\r\n");
          return;
        }
        else if (line.IsCommand("DATA"))
        {
          spool << ">> 354 Proceed" << endl;
          send_fully(sockfd, "354 Proceed\r\n");
          state = ClientState::Data;
        }
        else
        {
          spool << ">> 502 Unimplemented or unrecognized" << endl;
          send_fully(sockfd, "502 Unimplemented or unrecognized\r\n");
        }
        break;
      case ClientState::Data:
        if (line == ".")
        {
          spool << ">> 250 Done" << endl;
          send_fully(sockfd, "250 Done\r\n");
          state = ClientState::Initial;
        }
        break;
    }
  }
}

int main()
{
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);

  printf("Listen sockfd is %d\n", sockfd);

  int one = 1;

  int retval = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));

  printf("setsockopt() returned %d\n", retval);

  if (retval)
    printf("WARNING: Couldn't enable the SO_REUSEADDR option\n");

  struct sockaddr_in sin;

  memset(&sin, 0, sizeof(sin));

  sin.sin_len = sizeof(sin);
  sin.sin_family = AF_INET;
  //sin.sin_addr.s_addr = htonl(inet_addr("24.78.132.174"));
  sin.sin_port = htons(25);

  retval = bind(sockfd, (struct sockaddr *)&sin, sizeof(sin));

  printf("bind() returned %d\n", retval);

  if (retval != 0)
  {
    printf("Could not bind.\n");
    return 1;
  }

  retval = listen(sockfd, 20);

  printf("listen() returned %d\n", retval);

  if (retval != 0)
  {
    printf("Could not listen.\n");
    return 1;
  }

  while (true)
  {
    socklen_t sin_len = sizeof(sin);

    sin.sin_len = sin_len;

    printf("Calling accept()...");
    fflush(stdout);

    int client = accept(sockfd, (struct sockaddr *)&sin, &sin_len);

    printf(" got client fd %d\n", client);
    fflush(stdout);

    if (client <= 0)
    {
      printf("Could not accept.\n");
      return 1;
    }

    switch (fork())
    {
      case -1:
        printf("Could not fork.\n");
        return 1;
      case 0:
        close(sockfd);
        handle_client(client);
        return 0;
      default:
        close(client);
    }
  }
}

