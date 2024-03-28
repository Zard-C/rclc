// Copyright (c) 2020 - for information on the respective copyright owner
// see the NOTICE file and/or the repository https://github.com/micro-ROS/rclc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <std_msgs/msg/string.h>
#include <std_msgs/msg/int64.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>

/* This example show the usage of enable loaned message in subscription context
 * The loaned message is a message that is borrowed from the middleware and
 * returned after the callback is executed.
 * This is useful when the data type is plain old data  and 
 * the message is not needed after the callback is executed.
 */

/* Instead of creating some global variables,
 * we can define some data structures point to the local state info we care about as example in the previous snippet.
 */

typedef struct 
{
    int some_int;
    char* some_text;
} sub_context_t;



/* Callbacks */
void my_subscriber_callback_with_context(const void * msgin, void* context)
{
    const std_msgs__msg__Int64 *msg = (const std_msgs__msg__Int64*)msgin;
    if(msg == NULL)
    {
        printf("Callback: msg NULL\n");
    }  
    else 
    {
        printf("Callback: I heard: %ld, msg address: %p\n", msg->data, msg);
    }

    if(context == NULL)
    {
        printf("Callback: context is empty\n");
    }
    else
    {
        // cast the context pointer into the appropriate type
        sub_context_t *context_ptr = (sub_context_t *)context;
        // then you can access the context data
        printf("Callback: context contains: %s\n", context_ptr->some_text);
        printf("Callback: context also contains: %d\n", context_ptr->some_int);
        // this context data is in main(), and we can change it from here.
        context_ptr->some_int++;
    }
}

int main(int argc, const char* argv[])
{
    rcl_allocator_t allocator = rcl_get_default_allocator();
    rclc_support_t support;
    rcl_ret_t rc; 

    // within main, we can create the state information our subscriptions work with
    const unsigned int n_topics = 3;
    const char * topic_names[3] = {"topic_foo", "topic_bar", "topic_baz"};
    sub_context_t my_contexts[3] = {
        {0, "foo counting from zero"},
        {100, "bar counting from 100"},
        {200, "baz counting from 200"}
    };

    rcl_publisher_t my_pubs[n_topics];
    std_msgs__msg__Int64 pub_msgs[n_topics];
    rcl_subscription_t my_subs[n_topics];
    
    // no need to create a message for the publisher, as we will loan it from the middleware
    std_msgs__msg__Int64 sub_msgs[n_topics];

    // create init_options
    rc = rclc_support_init(&support, argc, argv, &allocator);
    if(rc != RCL_RET_OK)
    {
        printf("Error rclc_support_init.\n");
        return -1;
    }

    // create rcl_node
    rcl_node_t my_node = rcl_get_zero_initialized_node();
    rc = rclc_node_init_default(&my_node, "node_0", "executor_examples", &support);
    if(rc != RCL_RET_OK)
    {
        printf("Error in rclc_node_init_default\n");
        return -1;
    }

    const rosidl_message_type_support_t * my_type_support = 
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int64);
    
    // initialise each publisher and subcriber
    for(unsigned int i = 0; i < n_topics; i++)
    {
        rc = rclc_publisher_init_default(
            &(my_pubs[i]),
            &my_node,
            my_type_support,
            topic_names[i]);

        if(rc != RCL_RET_OK)
        {
            printf("Error in rclc_publisher_init_default\n");
            return -1;
        }   
        // assign message to publisher
        std_msgs__msg__Int64__init(&(pub_msgs[i]));
        pub_msgs[i].data = i;
    

        // create subscriptions
        my_subs[i] = rcl_get_zero_initialized_subscription();
        rc = rclc_subscription_init_default(
            &my_subs[i],
            &my_node,
            my_type_support,
            topic_names[i]
        );
        
        if(rc != RCL_RET_OK)
        {
            printf("Failed to create subscriber %s.\n", topic_names[i]);
            return -1;
        }
        else 
        {
            printf("Created subscriber %s\n", topic_names[i]);
        }

        // no need to create a message for the subscriber, as we will loan it from the middleware
        // std_msgs__msg__Int64__init(&sub_msgs[i]);

        // check the if the subscriptions are loaneable
        if(!rcl_subscription_can_loan_messages(&my_subs[i]))
        {
            printf("Error: subscription is not loaneable\n");
            return -1;
        }
        else
        {
            printf("subscription is loanable\n");
        }

        printf("sub_msgs[%u]: %p\n", i, (void*)&sub_msgs[i]);
    }

    // configuration of RCLC Executor
    rclc_executor_t executor;
    executor = rclc_executor_get_zero_initialized_executor();
    unsigned int num_handles = n_topics; // 3 subscriptions callback to schedule
    printf("Debug: number of DDS handles: %u\n", num_handles);
    rc = rclc_executor_init(&executor, &support.context, num_handles, &allocator);

    // add subscriptions to executor
    for(unsigned int i = 0; i < n_topics; i++)
    {
        sub_context_t * context_ptr =  &my_contexts[i];
        void * context_void_ptr = (void *)context_ptr;

        // add subscription to executor
        rc = rclc_executor_add_subscription_with_context(
            &executor,
            &my_subs[i],
            &sub_msgs[i],
            my_subscriber_callback_with_context,
            context_void_ptr,
            ON_NEW_DATA
        );

        if( rc != RCL_RET_OK)
        {
            printf("Error in rclc_executor_add_subscription_with_context\n");   
        }
    
    }

    for(unsigned int tick = 0; tick < 10; tick ++)
    {
        // timeout specified in nanoseconds (here 1s)
        rc = rclc_executor_spin_some(&executor, 1000 * (1000 * 1000));

        for( unsigned int i = 0; i < n_topics; i++)
        {
            rc = rcl_publish(&my_pubs[i], &pub_msgs[i], NULL);
            if(rc == RCL_RET_OK)
            {
                printf("\nPublished: %ld\n", pub_msgs[i].data);
            }
            else
            {
                printf("Error publishing message: %ld\n", pub_msgs[i].data);
            }
        }

        rc = rclc_executor_spin_some(&executor, 1000 * (1000 * 1000));
    }

    // clean up
    rc = rclc_executor_fini(&executor);

    for(unsigned int i = 0; i < n_topics; i++)
    {
        rc += rcl_publisher_fini(&my_pubs[i], &my_node);
        rc += rcl_subscription_fini(&my_subs[i], &my_node);
    }
    rc += rcl_node_fini(&my_node);
    rc += rclc_support_fini(&support);

    for(unsigned int i = 0; i < n_topics; i++)
    {
        std_msgs__msg__Int64__fini(&pub_msgs[i]);
    }


    if(rc != RCL_RET_OK)
    {
        printf("Error while cleaning up!\n");
        return -1;
    }
}