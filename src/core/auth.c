#include "auth.h"
#include "storage.h"
#include <string.h>

static Role_t current_role = ROLE_NONE;
static char operator_password[32] = "1234";
static char admin_password[32] = "admin";

int AuthInit(void)
{
    current_role = ROLE_NONE;

    // 尝试从数据库加载凭据，失败则使用内存默认值
    char loaded[32] = {0};
    if (StorageLoadCredential("admin", loaded, sizeof(loaded)) == 0 && loaded[0]) {
        strncpy(admin_password, loaded, sizeof(admin_password) - 1);
        admin_password[sizeof(admin_password) - 1] = '\0';
    } else {
        // 首次运行，保存默认密码
        StorageSaveCredential("admin", admin_password);
    }

    if (StorageLoadCredential("operator", loaded, sizeof(loaded)) == 0 && loaded[0]) {
        strncpy(operator_password, loaded, sizeof(operator_password) - 1);
        operator_password[sizeof(operator_password) - 1] = '\0';
    } else {
        StorageSaveCredential("operator", operator_password);
    }

    return 0;
}

int AuthLogin(const char *password)
{
    if (password == NULL)
        return -1;

    if (strcmp(password, admin_password) == 0)
    {
        current_role = ROLE_ADMIN;
        return 0;
    }
    else if (strcmp(password, operator_password) == 0)
    {
        current_role = ROLE_OPERATOR;
        return 0;
    }

    return -1;
}

Role_t AuthGetRole(void)
{
    return current_role;
}

void AuthLogout(void)
{
    current_role = ROLE_NONE;
}

const char *AuthGetRoleName(void)
{
    switch (current_role)
    {
    case ROLE_ADMIN:
        return "Admin";
    case ROLE_OPERATOR:
        return "Operator";
    default:
        return "Guest";
    }
}

int AuthIsAdmin(void)
{
    return (current_role == ROLE_ADMIN) ? 1 : 0;
}

int AuthChangeOperatorPassword(const char *new_password)
{
    if (new_password == NULL || strlen(new_password) == 0 || strlen(new_password) >= sizeof(operator_password))
        return -1;

    strncpy(operator_password, new_password, sizeof(operator_password) - 1);
    operator_password[sizeof(operator_password) - 1] = '\0';
    StorageSaveCredential("operator", operator_password);
    return 0;
}

int AuthChangeAdminPassword(const char *new_password)
{
    if (new_password == NULL || strlen(new_password) == 0 || strlen(new_password) >= sizeof(admin_password))
        return -1;

    strncpy(admin_password, new_password, sizeof(admin_password) - 1);
    admin_password[sizeof(admin_password) - 1] = '\0';
    StorageSaveCredential("admin", admin_password);
    return 0;
}
