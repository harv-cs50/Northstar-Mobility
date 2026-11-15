
#ifndef DATABASE_H
#define DATABASE_H

#include <pqxx/pqxx>
#include <string>

namespace vrms {

class Database {
private:
    pqxx::connection connection;

public:
    Database();

    bool isConnected() const;
    void testConnection();
};

}

#endif
