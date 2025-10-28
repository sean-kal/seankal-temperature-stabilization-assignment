#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <math.h>
#include "utils.h"

#define numExternals 4     // Number of external processes
#define EPS 1e-3f

static int listening_socket = -1;

int * establishConnectionsFromExternalProcesses()
{
    static int client_socket[numExternals];
    unsigned int client_size;
    struct sockaddr_in server_addr, client_addr;

    // Create socket:
    listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_socket < 0)
    {
        perror("Error creating socket");
        exit(1);
    }
    printf("[Server] Socket created successfully\n");

    // Set port and IP
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(2000);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    memset(&(server_addr.sin_zero), '\0', 8);

    // Bind to the set port and IP
    if (bind(listening_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Couldn't bind to the port");
        close(listening_socket);
        exit(1);
    }
    printf("[Server] Done with binding\n");

    // Listen for clients
    if(listen(listening_socket, 5) < 0)
    {
        perror("Error while listening");
        close(listening_socket);
        exit(1);
    }
    printf("\n[Server] Listening for incoming connections on 127.0.0.1:2000\n");

    printf("-------------------- Initial connections ---------------------------------\n");

    int externalCount = 0;
    while (externalCount < numExternals)
    {
        client_size = sizeof(client_addr);
        client_socket[externalCount] = accept(listening_socket, (struct sockaddr*)&client_addr, &client_size);

        if (client_socket[externalCount] < 0)
        {
            perror("Can't accept");
            close(listening_socket);
            exit(1);
        }

        printf("[Server] External process connected from %s:%d (slot %d)\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), externalCount);

        externalCount++;
    }
    printf("--------------------------------------------------------------------------\n");
    printf("[Server] All four external processes are now connected\n");
    printf("--------------------------------------------------------------------------\n\n");

    // Return pointer to the array of file descriptors of client sockets
    return client_socket;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: %s <initial_central_temperature>\n", argv[0]);
        return 1;
    }

    float centralTemp = atof(argv[1]);

    struct msg messageFromClient;

    // Establish client connections and return an array of file descriptors
    int * client_socket = establishConnectionsFromExternalProcesses();

    bool stable = false;
    bool first_iteration = true;
    float prev_temperatures[numExternals];

    // initialize prev_temperatures with nan so first iteration doesnt accidentally register as stable
    for (int i = 0; i < numExternals; i++) prev_temperatures[i] = NAN;

    while (!stable)
    {
        float temperature[numExternals];

        // Receive messages from the 4 external processes
        for (int i = 0;  i < numExternals; i++)
        {
            ssize_t r = recv(client_socket[i], (void *)&messageFromClient, sizeof(messageFromClient), 0);

            if (r <= 0)
            {
                if (r == 0)
                {
                    printf("[Server] External %d closed connection unexpectedly.\n", i+1);
                }
                else
                {
                    perror("[Server] recv");
                }
                // For this assignment we just exit on errors
                goto cleanup;
            }

            temperature[i] = messageFromClient.T;
            printf("[Server] Received temperature from External (%d) = %.6f\n", messageFromClient.Index, temperature[i]);
        }

        // Check stability condition (compare current temps with previous iteration)
        bool all_within_eps = true;
        if (!first_iteration)
        {
            for (int i = 0; i < numExternals; i++)
            {
                float diff = fabsf(temperature[i] - prev_temperatures[i]);
                if (diff >= EPS)
                {
                    all_within_eps = false;
                    break;
                }
            }
        }
        else
        {
            // cannot be stable on first iteration
            all_within_eps = false;
        }

        // Compute updated central temperature
        float sum = 0.0f;
        for (int i = 0; i < numExternals; i++) sum += temperature[i];

        float updatedCentral = (2.0f * centralTemp + sum) / 6.0f;
        printf("[Server] Central temp this iteration: old=%.6f updated=%.6f\n", centralTemp, updatedCentral);

        if (all_within_eps)
        {
            stable = true;
            centralTemp = updatedCentral; // adopt final central
            printf("\n[Server] System has stabilized. Final central temperature = %.6f\n\n", centralTemp);

            // Send done signal (index is -1) to each external and include final central temp in message.T
            struct msg done_msg = prepare_message(-1, centralTemp);
            for (int i = 0; i < numExternals; i++)
            {
                if (send(client_socket[i], (const void *)&done_msg, sizeof(done_msg), 0) < 0)
                {
                    perror("[Server] Can't send done message");
                }
            }

            break;
        }
        else
        {
            // When not stable, update centralTemp, send updated central temp to each external (index = 0)
            centralTemp = updatedCentral;
            struct msg updated_msg = prepare_message(0, centralTemp);

            for (int i = 0; i < numExternals; i++)
            {
                if (send(client_socket[i], (const void *)&updated_msg, sizeof(updated_msg), 0) < 0)
                {
                    perror("[Server] Can't send updated central temp");
                    goto cleanup;
                }
            }

            // save temps to prev_temperatures for next iteration
            for (int i = 0; i < numExternals; i++) prev_temperatures[i] = temperature[i];
            first_iteration = false;

            printf("\n");
            // continue next iteration where externals will send their updated temps
        }
    }

cleanup:
    // Close client sockets
    for (int i = 0; i < numExternals; i++)
    {
        if (client_socket[i] >= 0) close(client_socket[i]);
    }
    if (listening_socket >= 0) close(listening_socket);

    printf("[Server] Exiting.\n");

    return 0;
}
