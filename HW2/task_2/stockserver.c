#include "csapp.h"

void c11_itoa(int v, char *s, int b)
{
    int c, i = 0, j;
    while (20211584)
    {
        s[i++] = ((c = (v % b)) < 10) ? c + '0' : c - 10 + 'a';
        if ((v /= b) <= 0)
        {
            s[i] = '\0';
            break;
        }
    }

    int temp;
    for (i = 0, j = strlen(s) - 1; i < j; i++, j--)
    {
        temp = s[j];
        s[j] = s[i];
        s[i] = temp;
    }
}

int intMax(int A, int B)
{
    return (A > B) ? A : B;
}

#define ITEM_MAX 1000000
#define SBUF_SIZE 1000

typedef struct _Elem
{
    int ID, price, height, left_stock;
    int readcnt;
    sem_t mutex, w;
    struct _Elem *right;
    struct _Elem *left;
} Elem;

typedef struct
{
    int *buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
} sbuf_t;
sbuf_t sbuf;

typedef enum _command
{
    comm_show,
    comm_sell,
    comm_buy,
    comm_exit
} command;

Elem *root = NULL;         // root of AVL tree
Elem *show_elem[ITEM_MAX]; // comm_show array
int show_size;             // show_elem size

/* function headers */
void stock_load();
void stock_save();
command command_check(char *buf, int *id, int *amount);
void exec_command(int connfd, char *buf, int n);
void show_command(int connfd);
void sell_command(int connfd, int id, int amount);
void buy_command(int connfd, int id, int amount);
void exit_command(int connfd);
void sbuf_init(sbuf_t *sbufp, int n);
void sbuf_insert(sbuf_t *sbufp, int elem);
int sbuf_remove(sbuf_t *sbufp);
void sbuf_free(sbuf_t *sbufp);
void *thread(void *vargp);
void SIGINT_handler(int sig);

/* AVL functions */
Elem *AVL_insert(Elem *root, int id, int left_stock, int price);
Elem *AVL_search(Elem *node, int id);
void AVL_free(Elem *node);
int AVL_height(Elem *node);
Elem *AVL_L(Elem *node_2);
Elem *AVL_R(Elem *node_1);
Elem *AVL_LL(Elem *node);
Elem *AVL_RR(Elem *node);

Elem *AVL_insert(Elem *root, int id, int left_stock, int price)
{
    Elem *new;
    if (root == NULL)
    {
        new = (Elem *)Malloc(sizeof(Elem));
        new->ID = id;
        new->price = price;
        new->height = 0;
        new->left_stock = left_stock;
        new->left = new->right = NULL;
        new->readcnt = 0;
        Sem_init(&(new->mutex), 0, 1);
        Sem_init(&(new->w), 0, 1);
        show_elem[show_size++] = new;
        return new;
    }

    if (id > root->ID)
    {
        root->right = AVL_insert(root->right, id, left_stock, price);
        if ((AVL_height(root->right) - AVL_height(root->left)) == 2)
        {
            if (id <= root->right->ID)
                root = AVL_RR(root);
            else
                root = AVL_R(root);
        }
    }
    else if (id < root->ID)
    {
        root->left = AVL_insert(root->left, id, left_stock, price);
        if ((AVL_height(root->left) - AVL_height(root->right)) == 2)
        {
            if (id >= root->left->ID)
                root = AVL_LL(root);
            else
                root = AVL_L(root);
        }
    }
    root->height = intMax(AVL_height(root->left), AVL_height(root->right)) + 1;
    return root;
}

Elem *AVL_search(Elem *node, int id)
{
    if (node == NULL || node->ID == id)
        return node;
    if (node->ID < id)
        return AVL_search(node->right, id);
    else
        return AVL_search(node->left, id);
}

void AVL_free(Elem *node)
{
    if (node != NULL)
    {
        AVL_free(node->left);
        AVL_free(node->right);
        Free(node);
    }
    return;
}

int AVL_height(Elem *node)
{
    if (node == NULL)
        return -1;
    return node->height;
}

Elem *AVL_L(Elem *node_2)
{
    Elem *node_1 = NULL;
    node_1 = node_2->left;
    node_2->left = node_1->right;
    node_1->right = node_2;
    node_2->height = intMax(AVL_height(node_2->left), AVL_height(node_2->right)) + 1;
    node_1->height = intMax(AVL_height(node_1->left), AVL_height(node_2)) + 1;
    return node_1;
}

Elem *AVL_R(Elem *node_1)
{
    Elem *node_2 = NULL;
    node_2 = node_1->right;
    node_1->right = node_2->left;
    node_2->left = node_1;
    node_1->height = intMax(AVL_height(node_1->left), AVL_height(node_1->right)) + 1;
    node_2->height = intMax(AVL_height(node_2->right), AVL_height(node_1)) + 1;
    return node_2;
}

Elem *AVL_LL(Elem *node)
{
    node->left = AVL_R(node->left);
    return AVL_L(node);
}

Elem *AVL_RR(Elem *node)
{
    node->right = AVL_L(node->right);
    return AVL_R(node);
}
//////////////////////////////

void SIGINT_handler(int sig)
{
    int olderrno = errno;

    printf("\nexit processing...\n");
    stock_save();
    printf("stock.txt is saved.\n");
    AVL_free(root);
    printf("allocated memories are freed.\n");
    printf("exit process complete.\n");
    exit(0);

    errno = olderrno;
}

int main(int argc, char **argv)
{
    Signal(SIGINT, SIGINT_handler);

    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname[MAXLINE], client_port[MAXLINE];
    pthread_t tid;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    stock_load();

    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUF_SIZE);
    for (int i = 0; i < 500; i++)
        Pthread_create(&tid, NULL, thread, NULL); // spawn 500 threads
    while (1)
    {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        sbuf_insert(&sbuf, connfd);
    }

    return 0;
}

char *sell_success_message = "[sell] success\n";
char *buy_success_message = "[buy] success\n";
char *buy_fail_message = "Not enough left stock\n";

command command_check(char *buf, int *id, int *amount)
{
    char argument[10];
    sscanf(buf, "%s %d %d", argument, id, amount);
    if (!strcmp(argument, "show"))
        return comm_show;
    if (!strcmp(argument, "sell"))
        return comm_sell;
    if (!strcmp(argument, "buy"))
        return comm_buy;
    if (!strcmp(argument, "exit"))
        return comm_exit;
}

void exec_command(int connfd, char *buf, int n)
{
    int id, amount;

    switch (command_check(buf, &id, &amount))
    {
    case comm_show:
        show_command(connfd);
        break;
    case comm_sell:
        sell_command(connfd, id, amount);
        break;
    case comm_buy:
        buy_command(connfd, id, amount);
        break;
    case comm_exit:
        exit_command(connfd);
        break;
    default:
        break;
    }
}

void show_command(int connfd)
{
    char buf[MAXLINE] = "";

    for (int i = 0; i < show_size; i++)
    {
        char s1[100], s2[100], s3[100];

        P(&(show_elem[i]->mutex));
        (show_elem[i]->readcnt)++;
        if (show_elem[i]->readcnt == 1)
            P(&(show_elem[i]->w));
        V(&(show_elem[i]->mutex));

        c11_itoa(show_elem[i]->ID, s1, 10);
        c11_itoa(show_elem[i]->left_stock, s2, 10);
        c11_itoa(show_elem[i]->price, s3, 10);

        strcat(buf, s1);
        strcat(buf, " ");
        strcat(buf, s2);
        strcat(buf, " ");
        strcat(buf, s3);
        strcat(buf, "\n");

        P(&(show_elem[i]->mutex));
        (show_elem[i]->readcnt)--;
        if (show_elem[i]->readcnt == 0)
            V(&(show_elem[i]->w));
        V(&(show_elem[i]->mutex));
    }

    Rio_writen(connfd, buf, MAXLINE);
}

void buy_command(int connfd, int id, int amount)
{
    Elem *temp = AVL_search(root, id);
    char *buy_message;

    P(&(temp->w));
    if (temp->left_stock < amount)
        buy_message = buy_fail_message;
    else
    {
        temp->left_stock -= amount;
        buy_message = buy_success_message;
    }
    V(&(temp->w));

    Rio_writen(connfd, buy_message, MAXLINE);
}

void sell_command(int connfd, int id, int amount)
{
    Elem *temp = AVL_search(root, id);

    P(&(temp->w));
    temp->left_stock += amount;
    V(&(temp->w));

    Rio_writen(connfd, sell_success_message, MAXLINE);
}

void exit_command(int connfd)
{
    Rio_writen(connfd, "exit", MAXLINE);
}

void stock_load(void)
{
    int id, left_stock, price;
    char stock_buf[100];
    FILE *fp;

    if (!(fp = fopen("stock.txt", "rt")))
    {
        fprintf(stderr, "file open error(load).\n");
        exit(0);
    }

    while (Fgets(stock_buf, sizeof(stock_buf), fp))
    {
        sscanf(stock_buf, "%d %d %d", &id, &left_stock, &price);
        root = AVL_insert(root, id, left_stock, price);
    }

    Fclose(fp);
}

void stock_save(void)
{
    FILE *fp;

    if (!(fp = fopen("stock.txt", "wt")))
    {
        fprintf(stderr, "file open error(save).\n");
        exit(0);
    }

    for (int i = 0; i < show_size; i++)
    {
        char stock_buf[100];
        sprintf(stock_buf, "%d %d %d\n", show_elem[i]->ID, show_elem[i]->left_stock, show_elem[i]->price);
        Fputs(stock_buf, fp);
    }

    Fclose(fp);
}

void *thread(void *vargp)
{
    Pthread_detach(pthread_self());

    while (1)
    {
        int n, connfd = sbuf_remove(&sbuf);
        char buf[MAXLINE];
        rio_t rio;

        Rio_readinitb(&rio, connfd);
        while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
        {
            printf("server received %d bytes\n", n);
            exec_command(connfd, buf, n);
        }

        Close(connfd);
    }
}

void sbuf_init(sbuf_t *sbufp, int n)
{
    sbufp->buf = (int *)Calloc(n, sizeof(int));
    sbufp->n = n;
    sbufp->front = sbufp->rear = 0;
    Sem_init(&sbufp->mutex, 0, 1);
    Sem_init(&sbufp->slots, 0, n);
    Sem_init(&sbufp->items, 0, 0);
}

void sbuf_insert(sbuf_t *sbufp, int elem)
{
    P(&sbufp->slots);
    P(&sbufp->mutex);
    sbufp->buf[(++sbufp->rear) % (sbufp->n)] = elem;
    V(&sbufp->mutex);
    V(&sbufp->items);
}

int sbuf_remove(sbuf_t *sbufp)
{
    int elem;

    P(&sbufp->items);
    P(&sbufp->mutex);
    elem = sbufp->buf[(++sbufp->front) % (sbufp->n)];
    V(&sbufp->mutex);
    V(&sbufp->slots);

    return elem;
}

void sbuf_free(sbuf_t *sbufp)
{
    Free(sbufp->buf);
}
