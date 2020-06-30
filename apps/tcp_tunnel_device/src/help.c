//Warning this file is autogenrated by create_help.py
#include "help.h"
#include <stdio.h>
#define NEWLINE "\n"
void print_help() {
    printf("%s" NEWLINE, "TCP Tunnel Device Help.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "# Arguments.");
    printf("%s" NEWLINE, "  -h, --help       Print help");
    printf("%s" NEWLINE, "  -v, --version    Print version");
    printf("%s" NEWLINE, "  -H, --home-dir   Set alternative home dir, The default home dir is");
    printf("%s" NEWLINE, "                   $HOME/.nabto/edge on linux and mac, and %APPDATA%\\nabto\\edge");
    printf("%s" NEWLINE, "                   on windows");
    printf("%s" NEWLINE, "      --log-level  Set the log level for the application the possible levels");
    printf("%s" NEWLINE, "                   is error, warn, info and trace.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "# Files");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "The application uses several files. The are located in subfolders of");
    printf("%s" NEWLINE, "the homedir. Namely config, state and keys.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "The files is by default located in the folder unix:");
    printf("%s" NEWLINE, "`$HOME/.nabto/edge`, windows `%APPDATA%\\nabto\\edge`. The location can");
    printf("%s" NEWLINE, "be overriden by the home-dir option. In this case basefolder is");
    printf("%s" NEWLINE, "`${home-dir}`.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "## `config/device.json`");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "A configuration file containing the configuration for the device. This");
    printf("%s" NEWLINE, "includes the product id, device id and the host names of servers.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "The format of the json file is");
    printf("%s" NEWLINE, "```");
    printf("%s" NEWLINE, "{");
    printf("%s" NEWLINE, "  \"ProductId\": \"pr-abcd1234\",");
    printf("%s" NEWLINE, "  \"DeviceId\": \"de-abcd1234\",");
    printf("%s" NEWLINE, "  \"Server\": \"optional server hostname\"");
    printf("%s" NEWLINE, "}");
    printf("%s" NEWLINE, "```");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "The `ProductId` in the configuration is the product is which is");
    printf("%s" NEWLINE, "configured for the group of devices. The product id found in the cloud");
    printf("%s" NEWLINE, "console.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "The `DeviceId` is the device id for this specific device. This device");
    printf("%s" NEWLINE, "id found in the cloud console.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "The `Server` is an optional hostname of the server the device");
    printf("%s" NEWLINE, "uses. It not set the default server is used.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "## `config/tcp_tunnel_iam.json`");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "The `tcp_tunnel_iam.json` is an IAM policies file which contains the");
    printf("%s" NEWLINE, "policies and roles used by the system.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "TODO: link to further documentation of iam concepts.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "## `state/tcp_tunnel_state.json`");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "A file containing the state of the application, this file is written");
    printf("%s" NEWLINE, "by the application. A custom state file can be added to devices in");
    printf("%s" NEWLINE, "production such that the devices comes e.g. with some default state.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "## `keys/device.key`");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "The device key file is created if it does not exists.");
    printf("%s" NEWLINE, "");
    printf("%s" NEWLINE, "END OF GENERIC HELP");
}
