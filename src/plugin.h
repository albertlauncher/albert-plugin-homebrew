// Copyright (c) 2025-2026 Manuel Schneider

#pragma once
#include <albert/generatorqueryhandler.h>
#include <albert/plugin/applications.h>
#include <albert/extensionplugin.h>
#include <albert/plugindependency.h>
#include <chrono>
#include <mutex>

class Plugin : public albert::ExtensionPlugin, public albert::GeneratorQueryHandler
{
    ALBERT_PLUGIN

public:
    Plugin();

    QString defaultTrigger() const override;
    albert::ItemGenerator items(albert::QueryContext &context) override;

private:
    albert::StrongDependency<applications::Plugin> applications_plugin_;
    std::mutex cache_mutex_;
    std::chrono::time_point<std::chrono::system_clock> last_update_;
    std::vector<QString> package_names_;
};
