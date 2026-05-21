#ifndef __AUTH_H__
#define __AUTH_H__

typedef enum {
    ROLE_NONE = 0,
    ROLE_OPERATOR = 1,
    ROLE_ADMIN = 2
} Role_t;

int AuthInit(void);
int AuthLogin(const char *password);
Role_t AuthGetRole(void);
void AuthLogout(void);
const char *AuthGetRoleName(void);
int AuthIsAdmin(void);

int AuthChangeOperatorPassword(const char *new_password);
int AuthChangeAdminPassword(const char *new_password);

#endif
