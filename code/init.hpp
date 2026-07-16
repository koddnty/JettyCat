#pragma once
#include <chrono>
#include <semaphore>
#include <csignal>
#include "publicHeader.hpp"


const std::string& getInstanceId();

void signalHandler(int signum);

void projectInit();

void projectCleanUp(m_sylar::IOManager& iom, m_sylar::http::HttpServer::ptr server);