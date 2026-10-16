#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

// ================= 1. 单链表数据结构定义 =================

typedef struct MessageNode {
    char *role;                 // 角色: "system", "user", 或 "assistant"
    char *content;              // 对话内容
    struct MessageNode *next;   // 指向下一条消息的指针
} MessageNode;

// 链表尾插法：将新对话追加到历史记录末尾
void append_message(MessageNode **head, const char *role, const char *content) {
    MessageNode *new_node = (MessageNode *)malloc(sizeof(MessageNode));
    if (!new_node) {
        fprintf(stderr, "链表节点内存分配失败\n");
        return;
    }
    new_node->role = strdup(role);
    new_node->content = strdup(content);
    new_node->next = NULL;

    if (*head == NULL) {
        *head = new_node;
        return;
    }

    MessageNode *curr = *head;
    while (curr->next != NULL) {
        curr = curr->next;
    }
    curr->next = new_node;
}

// 释放整条链表的内存
void free_history(MessageNode *head) {
    MessageNode *curr = head;
    while (curr != NULL) {
        MessageNode *temp = curr;
        curr = curr->next;
        free(temp->role);
        free(temp->content);
        free(temp);
    }
}

// 计算当前链表长度，并在超出上限时删除最旧的对话（保留头节点 system）
void trim_history(MessageNode *head, int max_nodes) {
    if (head == NULL) return;

    int count = 0;
    MessageNode *curr = head;
    while (curr != NULL) {
        count++;
        curr = curr->next;
    }

    while (count > max_nodes && head->next != NULL) {
        MessageNode *oldest_msg = head->next;
        head->next = oldest_msg->next;

        free(oldest_msg->role);
        free(oldest_msg->content);
        free(oldest_msg);

        count--;
    }
}

// ================= 2. 网络接收与 JSON 解析 =================

struct ResponseBuffer {
    char *data;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    struct ResponseBuffer *mem = (struct ResponseBuffer *)userp;

    char *ptr = realloc(mem->data, mem->size + real_size + 1);
    if (!ptr) return 0;

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->data[mem->size] = '\0';

    return real_size;
}

void parse_and_print_response(const char *raw_json, MessageNode **head) {
    cJSON *json = cJSON_Parse(raw_json);
    if (!json) {
        fprintf(stderr, "JSON 解析失败\n");
        return;
    }

    cJSON *choices = cJSON_GetObjectItemCaseSensitive(json, "choices");
    if (cJSON_IsArray(choices)) {
        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItemCaseSensitive(first_choice, "message");
        cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");

        if (cJSON_IsString(content) && (content->valuestring != NULL)) {
            printf("\n【AI】: %s\n", content->valuestring);
            append_message(head, "assistant", content->valuestring);
        }
    } else {
        // 如果出错，打印服务端的错误信息
        cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
        if (error) {
            cJSON *msg = cJSON_GetObjectItemCaseSensitive(error, "message");
            if (msg) printf("\n[API 错误]: %s\n", msg->valuestring);
        } else {
            printf("\n[服务端异常返回]: %s\n", raw_json);
        }
    }
    cJSON_Delete(json);
}

// ================= 3. 主函数与核心循环 =================

int main(void) {
    const char *raw_key = getenv("DEEPSEEK_API_KEY");
    if (!raw_key) {
        fprintf(stderr, "错误：未检测到环境变量 DEEPSEEK_API_KEY！\n");
        fprintf(stderr, "请先在终端执行: export DEEPSEEK_API_KEY=\"你的sk-密钥\"\n");
        return 1;
    }
    
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", raw_key);

    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl 初始化失败\n");
        return 1;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header); // <-- 加上了这一行鉴权头！
    
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.deepseek.com/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

    MessageNode *history_head = NULL;
    append_message(&history_head, "system", "你是一个贴心、简洁的编程助手。");

    char user_input[1024];
    printf("\n==============================================\n");
    printf("  欢迎使用 C 语言 AI 终端助手 (滑动窗口记忆版)\n");
    printf("  输入 'exit' 退出程序\n");
    printf("==============================================\n");

    while (1) {
        printf("\n【你】: ");
        if (fgets(user_input, sizeof(user_input), stdin) == NULL) break;

        user_input[strcspn(user_input, "\n")] = 0;

        if (strcmp(user_input, "exit") == 0) {
            printf("正在清理内存并退出... 再见！\n");
            break;
        }
        if (strlen(user_input) == 0) continue;

        append_message(&history_head, "user", user_input);

        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "model", "deepseek-chat");
        
        cJSON *messages = cJSON_CreateArray();
        MessageNode *curr = history_head;
        while (curr != NULL) {
            cJSON *msg = cJSON_CreateObject();
            cJSON_AddStringToObject(msg, "role", curr->role);
            cJSON_AddStringToObject(msg, "content", curr->content);
            cJSON_AddItemToArray(messages, msg);
            curr = curr->next;
        }
        
        cJSON_AddItemToObject(root, "messages", messages);
        char *json_payload = cJSON_PrintUnformatted(root);

        struct ResponseBuffer buffer;
        buffer.data = malloc(1);
        buffer.data[0] = '\0';
        buffer.size = 0;
        
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buffer);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "请求失败: %s\n", curl_easy_strerror(res));
        } else {
            // 解析回答，并顺手把 AI 的回答挂到链表尾部，同时触发滑动窗口裁剪
            parse_and_print_response(buffer.data, &history_head);
            trim_history(history_head, 7);
        }

        free(buffer.data);
        free(json_payload);
        cJSON_Delete(root);
    }

    free_history(history_head);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return 0;
}
