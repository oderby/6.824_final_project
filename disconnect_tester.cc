//
// Disconnect test client
//

#include "lock_protocol.h"
#include "disconnect_client.h"
#include "rpc.h"
#include <arpa/inet.h>
#include <vector>
#include <stdlib.h>
#include <stdio.h>
#include <string>
using namespace std;

disconnect_client *lc;

int
main(int argc, char *argv[])
{
  int r;

  if(argc != 3){
    fprintf(stderr, "Usage: %s [host:]port disconnect?\n", argv[0]);
    exit(1);
  }

  lc = new disconnect_client(argv[1]);
  string command(argv[2]);
  if (command == "1" || command == "y") {
    r = lc->disconnect(true);
    printf ("disconnect returned %d\n", r);
  } else if (command == "0" || command == "n") {
    r = lc->disconnect(false);
    printf ("disconnect returned %d\n", r);
  } else {
    fprintf(stderr, "Unknown command %s\n", argv[2]);
  }
  exit(0);
}
