#ifndef JOT_LIST
#define JOT_LIST

//An implementation of type generic intrusive linked list structures 
// stolen from Ryan Fleury's codebase. This file is self contained.
// 
// We define two variants: 
//   list_push_nil variant allowing a specifiication of custom nil value and more control
//   list_push variant that will most often be used
//
// We implement the following structures:
//  1: Chain - the simplest linked list allowing only stack order push pop
//  2: List - the most used list type allowing pushing from both ends and popping from front
//  3: BiList (Bidirectional list) - doublely-linked list allowing push and pop from 
//     both sides, as well as addition/removal to the middle
//
// All functions are preceeded by an assert checking precodnditions. 
// Due to the macro limitations we only check local properties of the list 
// (ie. no checkcing of traversibility of the entire list)

#include <stddef.h>

//#define to enable extra checks 
//#define LIST_DEBUB

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x) assert(x)
#endif

#ifndef ASSERT_SLOW
    #define ASSERT_SLOW(x) ASSERT(x)
#endif

#ifdef LIST_DEBUB
    #define _is_properly_linked(node, next, prev, NULL) (1                                  \
            && ((node) != NULL && (node)->next != NULL ? (node)->next->prev == (node) : 1)  \
            && ((node) != NULL && (node)->prev != NULL ? (node)->prev->next == (node) : 1)  \
        )                                                                                   
#else
    #define _is_properly_linked(node, next, prev, NULL) 1
#endif

//Chain
#define chain_push_nil(first, node, next, NULL)                     \
    ASSERT("node must not be null and izolated"                     \
        && (node) != NULL                                           \
        /* && (node)->next == NULL*/                                     \
    ),                                                              \
    ((node)->next = (first), (first)=(node))                        \

#define chain_pop_nil(first, next, NULL)                            \
    /* No preconditions! */                                         \
    ((first) == NULL ? 0:((first)=(first)->next))                   \


//List
#define list_push_nil(first, last, node, next, NULL) (              \
    ASSERT("node must not be null and izolated, list must be valid" \
        && (node) != NULL                                           \
        && (node)->next == NULL                                     \
        && ((first) == NULL) == ((last) == NULL)                    \
    ),                                                              \
    (first) == NULL                                                 \
        ? ((first) = (last) = (node), (node)->next = NULL)          \
        : ((last)->next=(node), (last)=(node), (node)->next = NULL) \
    )                                                               \
    
#define list_push_front_nil(first, last, node, next, NULL) (        \
    ASSERT("node must not be null and izolated, list must be valid" \
        && (node) != NULL                                           \
        /* && (node)->next == NULL */                               \
        && ((first) == NULL) == ((last) == NULL)                    \
    ), \
    (first) == NULL                                                 \
        ? ((first) = (last) = (node), (node)->next = NULL)          \
        : ((node)->next = (first)), ((first) = (node))              \
    )                                                               \

#define list_pop_nil(first, last, next, NULL) (                     \
    ASSERT("list must be valid"                                     \
        && ((first) == NULL) == ((last) == NULL)                    \
    ),                                                              \
    (first)==(last)                                                 \
        ? ((first) = NULL, (last) = NULL)                           \
        : ((first)=(first)->next)                                   \
    )                                                               \


//Bilist
#define bilist_insert_nil_cond(first, last, after, insert_first, node, next, prev, NULL) (      \
    ASSERT("node must not be null and izolated, after must be properly linked," \
           "list must be valid"                                                 \
        && (node) != NULL                                                       \
        /*&& (node)->next == NULL && (node)->prev == NULL*/                     \
        && ((first) == NULL) == ((last) == NULL)                                \
        && _is_properly_linked((after),next,prev,NULL)                          \
        && _is_properly_linked((first),next,prev,NULL)                          \
        && _is_properly_linked((last),next,prev,NULL)                           \
    ),                                                                          \
    (first) == NULL                                                             \
        ? (                                                                     \
            (first) = (last) = (node),                                          \
            (node)->next = NULL,                                                \
            (node)->prev = NULL                                                 \
        )                                                                       \
        : (insert_first)                                                        \
            ? (                                                                 \
                (node)->prev = NULL,                                            \
                (node)->next = (first),                                         \
                ((first) != NULL ?                                              \
                    (first)->prev = (node) : 0),                                \
                (first) = (node)                                                \
            )                                                                   \
            : (                                                                 \
                (((after)->next) != NULL                                        \
                    ? ((after)->next->prev) = (node) : 0),                      \
                (node)->next = (after)->next,                                   \
                (node)->prev = (after),                                         \
                (after)->next = (node),                                         \
                ((after) == (last)                                              \
                    ? (last) = (node) : 0)                                      \
            )                                                                   \
    )                                                                           \

#define bilist_insert_nil(first, last, after, node, next, prev, NULL) \
    bilist_insert_nil_cond((first), (last), (after), (after) == NULL, (node), next, prev, NULL);

#define bilist_remove_nil(first, last, node, next, prev, NULL) (                \
    ASSERT("node must not be null and must be properly linked. List must be valid"                  \
        && (node) != NULL                                                       \
        && ((first) == NULL) == ((last) == NULL)                                \
        && _is_properly_linked((node),next,prev,NULL)                           \
        && _is_properly_linked((first),next,prev,NULL)                          \
        && _is_properly_linked((last),next,prev,NULL)                           \
    ),                                                                          \
    (first) == (node)                                                           \
        ? (                                                                     \
            ((first) = (first)->next),                                          \
            ((first) == NULL                                                    \
                ? ((last) = NULL)                                               \
                : ((first)->prev = NULL))                                       \
        )                                                                       \
        : (last) == (node)                                                      \
            ? (                                                                 \
                ((last) = (last)->prev),                                        \
                ((last) == NULL                                                 \
                    ? ((first) = NULL)                                          \
                    : ((last)->next = NULL))                                    \
            )                                                                   \
            : (                                                                 \
                ((node)->next != NULL ? (node)->next->prev = (node)->prev : 0), \
                ((node)->prev != NULL ? (node)->prev->next = (node)->next : 0)  \
            )                                                                   \
    )                                                                           \
    //,((node)->next = (node)->prev = NULL), (node)                                \
    
#define bilist_push_back_nil(first, last, node, next, prev, NULL) bilist_insert_nil_cond((first),(last),(last),0,(node),next,prev,NULL)
#define bilist_push_front_nil(first, last, node, next, prev, NULL) bilist_insert_nil_cond((first),(last),(last),1,(node),next,prev,NULL)

#define bilist_pop_back_nil(first, last, next, prev, NULL) ((last) != NULL ? bilist_remove_nil((first),(last),(last),next,prev,NULL), 1 : 0)
#define bilist_pop_front_nil(first, last, next, prev, NULL) ((first) != NULL ? bilist_remove_nil((first),(last),(first),next,prev,NULL), 1 : 0)

//Non NIL variants

//Chain
#define chain_push(first, node)                 chain_push_nil(*(first),(node),next,NULL)
#define chain_pop(first)                        chain_pop_nil(*(first),next,NULL)

//List
#define list_push(first, last, node)            list_push_nil(*(first),*(last),(node),next,NULL) 
#define list_push_front(first, last, node)      list_push_front_nil(*(first),*(last),(node),next,NULL)
#define list_pop(first, last)                   list_pop_nil(*(first),*(last),next,NULL) 

//Bilist
#define bilist_insert(first, last, after, node) bilist_insert_nil(*(first),*(last),(after),(node),next,prev,NULL)
#define bilist_remove(first, last, node)        bilist_remove_nil(*(first),*(last),(node),next,prev,NULL)

#define bilist_push_back(first, last, node)     bilist_push_back_nil(*(first),*(last),(node),next,prev,NULL)
#define bilist_push_front(first, last, node)    bilist_push_front_nil(*(first),*(last),(node),next,prev,NULL)

#define bilist_pop_back(first, last)            bilist_pop_back_nil(*(first),*(last),next,prev,NULL)
#define bilist_pop_front(first, last)           bilist_pop_front_nil(*(first),*(last),next,prev,NULL)

#endif

#if (defined(JOT_ALL_TEST) || defined(JOT_LIST_TEST)) && !defined(JOT_LIST_HAS_TEST)
#define JOT_LIST_HAS_TEST
static void test_list()
{
    enum {NODES = 10};
    {
        typedef struct Node {
            int val;
            int _padding;
            struct Node* next;
        } Node;
        Node* first = NULL;
        Node* last = NULL;

        Node nodes[NODES] = {0};
        for(int i = 0; i < NODES; i++)
            nodes[i].val = i;
        
        for(int i = 0; i < NODES; i++)
            list_push_front(&first, &last, &nodes[i]);

        //now the list looks like: NODES -1, ... 2, 1, 0, 0, 1, 2, ... NODES - 1
        // so popping from front should first produce descending series 
        ASSERT(first != NULL);
        ASSERT(last != NULL);
        for(int i = 0; i < NODES; i++)
        {
            int first_val = first->val;
            ASSERT(first_val == NODES - i - 1);
            list_pop(&first, &last);
        }
        
        for(int i = 0; i < NODES; i++)
        {
            nodes[i].next = NULL;
            list_push(&first, &last, &nodes[i]);
        }
            
        //... and then ascending series
        for(int i = 0; i < NODES; i++)
        {
            int first_val = first->val;
            ASSERT(first_val == i);
            list_pop(&first, &last);
        }

        ASSERT(first == NULL);
        ASSERT(last == NULL);
    }   
    
    {
        typedef struct BiNode {
            int val;
            int _padding;
            struct BiNode* next;
            struct BiNode* prev;
        } BiNode;

        BiNode* first = NULL;
        BiNode* last = NULL;

        BiNode nodes[NODES] = {0};
        for(int i = 0; i < NODES; i++)
            nodes[i].val = i;
        
        //push_back, pop_front
        for(int i = 0; i < NODES; i++)
            bilist_push_back(&first, &last, &nodes[i]);
            
        ASSERT(first != NULL);
        ASSERT(last != NULL);
        for(int i = 0; i < NODES; i++)
        {
            int first_val = first->val;
            ASSERT(first_val == i);
            bilist_pop_front(&first, &last);
        }

        ASSERT(first == NULL);
        ASSERT(last == NULL);
        
        //push_front, pop_back
        for(int i = 0; i < NODES; i++)
            bilist_push_front(&first, &last, &nodes[i]);

        ASSERT(first != NULL);
        ASSERT(last != NULL);
        for(int i = 0; i < NODES; i++)
        {
            int popped = last->val;
            ASSERT(popped == i);
            bilist_pop_back(&first, &last);
        }
    }   
}
#endif