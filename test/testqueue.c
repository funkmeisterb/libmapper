
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

mapper_device source = 0;
mapper_device destination = 0;
mapper_router router = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;
mapper_signal sendsig1 = 0;
mapper_signal recvsig1 = 0;


int port = 9000;

int sent = 0;
int received = 0;

int setup_source()
{
    source = mdev_new("testsend", port, 0);
    if (!source)
        goto error;
    printf("source created.\n");

    float mn=0, mx=1;

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', 0, &mn, &mx);
    sendsig1= mdev_add_output(source, "/outsig1", 1, 'f', 0, &mn, &mx);

	printf("Output signal /outsig registered.\n");
    printf("Number of outputs: %d\n", mdev_num_outputs(source));
    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        if (router) {
            printf("Removing router.. ");
            fflush(stdout);
            mdev_remove_router(source, router);
            printf("ok\n");
        }
        printf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        printf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        printf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_destination()
{
    destination = mdev_new("testrecv", port, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mdev_add_input(destination, "/insig", 1, 'f', 0,
                             &mn, &mx, insig_handler, 0);
	recvsig1= mdev_add_input(destination, "/insig1", 1, 'f', 0,
                             &mn, &mx, insig_handler, 0);

    printf("Input signal /insig registered.\n");
    printf("Number of inputs: %d\n", mdev_num_inputs(destination));
    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        printf("Freeing destination.. ");
        fflush(stdout);
        mdev_free(destination);
        printf("ok\n");
    }
}

int setup_router()
{
    const char *host = "localhost";
    router = mapper_router_new(source, host, destination->admin->port,
                               mdev_name(destination), 1);
    /* router = mapper_router_new(source, "127.0.0.1", 9001, */
    /*                            mdev_name(destination), 0); */
    mdev_add_router(source, router);
    printf("Router to %s:%d added.\n", host, port);

    char signame_in[1024];
    if (!msig_full_name(recvsig, signame_in, 1024)) {
        printf("Could not get destination signal name.\n");
        return 1;
    }

    char signame_out[1024];
    if (!msig_full_name(sendsig, signame_out, 1024)) {
        printf("Could not get source signal name.\n");
        return 1;
    }

	char signame_in1[1024];
    if (!msig_full_name(recvsig1, signame_in1, 1024)) {
        printf("Could not get destination signal name.\n");
        return 1;
    }

    char signame_out1[1024];
    if (!msig_full_name(sendsig1, signame_out1, 1024)) {
        printf("Could not get source signal name.\n");
        return 1;
    }

    printf("Connecting signal %s -> %s\n", signame_out, signame_in);
    mapper_connection c = mapper_router_add_connection(router, sendsig,
                                                       recvsig->props.name,
                                                       'f', 1);
    printf("Connecting signal %s -> %s\n", signame_out1, signame_in1);
    mapper_connection c1 = mapper_router_add_connection(router, sendsig1,
                                                       recvsig1->props.name,
                                                       'f', 1);

    mapper_connection_range_t range;
    range.src_min = 0;
    range.src_max = 1;
    range.dest_min = -10;
    range.dest_max = 10;
    range.known = CONNECTION_RANGE_KNOWN;
    
    mapper_connection_set_linear_range(c, &range);
	mapper_connection_set_linear_range(c1, &range);

    return 0;
}

void wait_ready()
{
    while (!(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);
        usleep(500 * 1000);
    }
}

void loop()
{
    printf("Polling device..\n");
	int i;
	float j=1;
	for (i = 0; i < 10; i++) {
        j=i;
        mapper_timetag_t now;
        mdev_timetag_now(source, &now);
        mdev_start_queue(source, now);
		mdev_poll(source, 0);
        printf("Updating signal %s to %f\n",
               sendsig->props.name, j);
        msig_update(sendsig, &j, 0, now);
		msig_update(sendsig1, &j, 0, now);
		mdev_send_queue(sendsig->device, now);
		sent = sent+2;
        usleep(250 * 1000);
        mdev_poll(destination, 0);
    }

}
int main()
{
    int result = 0;

    if (setup_destination()) {
        printf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        printf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_ready();

    if (setup_router()) {
        printf("Error initializing router.\n");
        result = 1;
        goto done;
    }

    loop();

    if (sent != received) {
        printf("Not all sent messages were received.\n");
        printf("Updated value %d time%s, but received %d of them.\n",
               sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup_destination();
    cleanup_source();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
