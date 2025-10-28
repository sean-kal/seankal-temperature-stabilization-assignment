#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include "utils.h"

int main (int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s <external_index 1-4> <initial_temperature>\n", argv[0]);
        return 1;
    }

    int externalIndex = atoi(argv[1]);
    float externalTemp = atof(argv[2]);

    int socket_desc;
    struct sockaddr_in server_addr;
    struct msg the_message;

    // Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc < 0)
    {
        perror("Unable to create socket");
        return -1;
    }

    // Set port and IP same as server
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(2000);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect
    if (connect(socket_desc, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Unable to connect");
        return -1;
    }
    printf("[Client %d] Connected to server.\n", externalIndex);

    // Send initial temperature
    the_message = prepare_message(externalIndex, externalTemp);
    if (send(socket_desc, (const void *)&the_message, sizeof(the_message), 0) < 0)
    {
        perror("Unable to send initial temperature");
        close(socket_desc);
        return -1;
    }
    printf("[Client %d] Sent initial temperature = %.6f\n", externalIndex, externalTemp);

    // Loop until server sends done
    while (1)
    {
        struct msg server_msg;
        ssize_t r = recv(socket_desc, (void *)&server_msg, sizeof(server_msg), 0);
        if (r <= 0)
        {
            if (r == 0)
            {
                printf("[Client %d] Server closed connection.\n", externalIndex);
            }
            else
            {
                perror("[Client] recv");
            }

            break;
        }

        // If server signals done (index is -1), print final and exit
        if (server_msg.Index == -1)
        {
            // central sent final central temperature in server_msg.T
            printf("[Client %d] Received DONE signal from central. Final temperature (central) = %.6f\n", externalIndex, server_msg.T);
            printf("[Client %d] Final external temperature = %.6f\n", externalIndex, externalTemp);
            break;
        }

        // Otherwise server_msg.T is the central temperature for this iteration
        float central = server_msg.T;

        // Update external temperature: external = (3*external + 2*central) / 5
        float new_external = (3.0f * externalTemp + 2.0f * central) / 5.0f;
        printf("[Client %d] Received central=%.6f -> updating external %.6f -> %.6f\n",
               externalIndex, central, externalTemp, new_external);
        externalTemp = new_external;

        // Send updated temperature back to server
        struct msg out = prepare_message(externalIndex, externalTemp);
        if (send(socket_desc, (const void *)&out, sizeof(out), 0) < 0)
        {
            perror("Unable to send updated temperature");
            break;
        }
    }

    close(socket_desc);
    printf("[Client %d] Exiting.\n", externalIndex);

    return 0;
}
