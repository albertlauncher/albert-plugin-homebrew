// Copyright (c) 2025-2026 Manuel Schneider

#include "plugin.h"
#include <QCoroGenerator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <albert/icon.h>
#include <albert/logging.h>
#include <albert/matcher.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
#include <ranges>
ALBERT_LOGGING_CATEGORY("homebrew")
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std::chrono;
using namespace std;

namespace {
applications::Plugin *applications_plugin;
static const auto brew = u"brew"_s;
static const auto sep = u" · "_s;
static unique_ptr<Icon> makeDefaultIcon() { return Icon::grapheme(u"📦"_s); };
}

// -------------------------------------------------------------------------------------------------

class BrewItem : public Item
{
public:

    BrewItem(const QJsonObject &info, const QString &name, bool installed):
        info_(info),
        name_(name),
        desc_(info["desc"_L1].toString()),
        installed_(installed),
        outdated_(info["outdated"_L1].toBool()),
        disabled_(info["disabled"_L1].toBool())
    {

    }

    QString text() const override { return name_; }

    unique_ptr<Icon> icon() const override
    {
        if (disabled_)
            return Icon::composed(makeDefaultIcon(), Icon::grapheme(u"🛑"_s), 1., .4);
        else if (outdated_)
            return Icon::composed(makeDefaultIcon(), Icon::grapheme(u"⚠️"_s), 1., .4);
        else if (installed_)
            return Icon::composed(makeDefaultIcon(), Icon::grapheme(u"✅"_s), 1., .4);
        else
            return makeDefaultIcon();
    }

    vector<Action> actions() const override {
        vector<Action> actions;
        if (!disabled_)
        {
            if (installed_)
                actions.emplace_back(
                    u"uninstall"_s,
                    Plugin::tr("Uninstall"),
                    [this] {
                        applications_plugin->runTerminal(
                            u"%1 uninstall %2 || exec $SHELL"_s.arg(brew, name_));
                    }
                );
            else
                actions.emplace_back(
                    u"install"_s,
                    Plugin::tr("Install"),
                    [this] {
                        applications_plugin->runTerminal(
                            u"%1 install %2 || exec $SHELL"_s.arg(brew, name_));
                    }
                );
        }

        actions.emplace_back(
            u"info_local"_s,
            Plugin::tr("Info (Terminal)"),
            [this] {
                applications_plugin->runTerminal(
                    u"%1 info %2 ; exec $SHELL"_s.arg(brew, name_));
            });

        actions.emplace_back(
            u"homepage"_s,
            Plugin::tr("Project homepage"),
            [this] { openUrl(info_["homepage"_L1].toString()); }
            );

        return actions;
    }

    Action makeUnInstallAction() const {
        if (installed_)
            return {
                u"uninstall"_s,
                Plugin::tr("Uninstall"),
                [this] {
                    applications_plugin->runTerminal(
                        u"%1 uninstall %2 || exec $SHELL"_s.arg(brew, name_));
                }
            };
        else
            return {
                u"install"_s,
                Plugin::tr("Install"),
                [this] {
                    applications_plugin->runTerminal(
                        u"%1 install %2 || exec $SHELL"_s.arg(brew, name_));
                }
            };
    }

    inline const QJsonObject info() { return info_; }

    auto makeSubtext(const QString &type) const
    {
        auto subtext_tokens = QStringList(type);
        if (installed_)
            subtext_tokens << Plugin::tr("Installed");
        if (outdated_)
            subtext_tokens << Plugin::tr("Outdated");
        if (disabled_)
            subtext_tokens << Plugin::tr("DISABLED");
        if (!desc_.isEmpty())
            subtext_tokens << desc_;
        return subtext_tokens.join(sep);
    }

protected:
    const QJsonObject info_;
    QString name_;
    QString desc_;
    bool installed_;
    bool outdated_;
    bool disabled_;
};

class CaskItem : public BrewItem
{
public:
    // Example: `brew info --json=v2 google-chrome | jq '.casks.[0]'`
    CaskItem(const QJsonObject &info) :
        BrewItem(info,
                 info["token"_L1].toString(),
                 !info["installed"_L1].isNull())
    {}

    QString id() const override { return u"c."_s + name_; }

    QString subtext() const override { return BrewItem::makeSubtext(Plugin::tr("Cask")); }

    vector<Action> actions() const override {
        auto actions = BrewItem::actions();

        actions.emplace_back(
            u"info_online"_s,
            Plugin::tr("Info (Browser)"),
            [this] { openUrl(u"https://formulae.brew.sh/cask/%1"_s.arg(name_)); }
            );

        return actions;
    }
};

class FormulaItem : public BrewItem
{
public:
    // Example: `brew info --json=v2 xz | jq '.formulae.[0]'`
    FormulaItem(const QJsonObject &info) :
        BrewItem(info,
                 info["name"_L1].toString(),
                 !info["installed"_L1].toArray().isEmpty())
    {}

    QString id() const override { return u"f."_s + name_; }

    QString subtext() const override { return BrewItem::makeSubtext(Plugin::tr("Formula")); }

    vector<Action> actions() const override {
        auto actions = BrewItem::actions();

        actions.emplace_back(
            u"info_online"_s,
            Plugin::tr("Info (Browser)"),
            [this] { openUrl(u"https://formulae.brew.sh/formula/%1"_s.arg(name_)); }
            );

        return actions;
    }
};

// -------------------------------------------------------------------------------------------------

Plugin::Plugin() :
    applications_plugin_(u"applications"_s)
{
    applications_plugin = applications_plugin_.get();  // global for convenience

    if (auto exec = QStandardPaths::findExecutable(u"brew"_s); exec.isEmpty())
        throw runtime_error("Homebrew executable not found.");
    else
        DEBG << "Found Homebrew executable at" << exec;
}

QString Plugin::defaultTrigger() const { return brew + QChar::Space; }

static vector<QString> getPackageNames()
{
    QProcess cask_proc;
    cask_proc.start(brew, {u"casks"_s});

    QProcess formula_proc;
    formula_proc.start(brew, {u"formulae"_s});

    vector<QString> package_names;
    for (auto *proc : {&cask_proc, &formula_proc}){
        proc->waitForFinished();
        QTextStream ts(proc);
        while (!ts.atEnd())
            package_names.emplace_back(ts.readLine());
    }
    return package_names;
}

ItemGenerator Plugin::items(albert::QueryContext &ctx)
{
    const auto query = ctx.query().trimmed();

    if (query.isEmpty())
    {
        const auto desc = u"Update and upgrade."_s;
        auto item = StandardItem::make(u"update"_s,
                                       tr("Update"),
                                       desc,
                                       [] {
                                           return Icon::composed(makeDefaultIcon(),
                                                                   Icon::grapheme(u"⬆️"_s),
                                                                   1.,
                                                                   .4);
                                       },
                                       {{u"update"_s, u"Update"_s, [] {
                                             applications_plugin->runTerminal(
                                                 u"%1 update && %1 upgrade"_s.arg(brew));
                                         }}});
        vector<shared_ptr<Item>> items;
        items.emplace_back(::move(item));
        co_yield ::move(items); // TODO(26.04): Remove lvalue workaround
        co_return;
    }

    vector<pair<QString, Match>> ranked_names;
    {
        lock_guard lock(cache_mutex_);

        if (auto now = system_clock::now();
            duration_cast<minutes>(now - last_update_) > 1min){
            last_update_ = system_clock::now();
            package_names_ = getPackageNames();
        }

        Matcher matcher(ctx);
        for (const auto &name : package_names_)
            if (auto match = matcher.match(name); match)
                ranked_names.emplace_back(name, match);
    }
    ranges::sort(ranked_names, less{}, &pair<QString, Match>::second);

    while(!ranked_names.empty())
    {
        auto v =
            ranked_names
                 | views::reverse
                 | views::take(10)
                 | views::transform([](const auto &ranked_name){
                       return get<QString>(ranked_name);
                   }); // TODO ranges::to<>
        const QStringList names(begin(v), end(v));

        QProcess proc;
        proc.start(brew,  QStringList{u"info"_s, u"--json=v2"_s} << names);
        while(!proc.waitForFinished(10))
            if (!ctx.isValid())
            {
                proc.terminate();
                co_return;
            }

        const auto doc = QJsonDocument::fromJson(proc.readAllStandardOutput()).object();
        const auto cask_infos = doc[u"casks"_s].toArray();
        const auto fomula_infos = doc[u"formulae"_s].toArray();

        vector<shared_ptr<Item>> items;
        for (const auto &name : names)
        {
            if (auto it = ranges::find_if(cask_infos,
                                          [&](const auto &obj){ return obj[u"token"_s] == name; });
                it != cask_infos.end())
                items.emplace_back(make_shared<CaskItem>(it->toObject()));

            if (auto it = ranges::find_if(fomula_infos,
                                          [&](const auto &obj){ return obj[u"name"_s] == name; });
                it != fomula_infos.end())
                items.emplace_back(make_shared<FormulaItem>(it->toObject()));
        }

        // Cheap pop_n
        ranked_names.erase(ranked_names.end() - names.size(), ranked_names.end());

        co_yield ::move(items);
    }
}
