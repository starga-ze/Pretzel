#pragma once

#include <optional>
#include <string>
#include <vector>

struct pg_conn;
typedef struct pg_conn PGconn;

namespace pz::db
{

struct ConnParams
{
    std::string host = "127.0.0.1";
    std::string port = "5432";
    std::string name = "pretzel";
    std::string user = "pretzel";
    std::string password = "pretzel";
};

class Database
{
public:
    static Database& instance();

    bool connect(const ConnParams& params);

    bool isConnected();

    bool ensureSchema();

    bool exec(const std::string& sql, const std::vector<std::string>& params = {});

    std::optional<std::string> queryScalar(const std::string& sql, const std::vector<std::string>& params = {});

    std::vector<std::vector<std::string>> queryRows(const std::string& sql,
                                                    const std::vector<std::string>& params = {});

private:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool ensureLive();

    PGconn* m_conn{nullptr};
    ConnParams m_params;
    bool m_haveParams{false};
};

}
