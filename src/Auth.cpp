#include "stdafx.h"
#include "Auth.h"

bool CAuth::Check(const string &user, const string &pass) {
    return (user == "xbox" && pass == "xbox");
}
