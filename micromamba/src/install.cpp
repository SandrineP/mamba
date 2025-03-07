#include "mamba/api/configuration.hpp"
#include "mamba/api/install.hpp"

#include "common_options.hpp"


using namespace mamba;  // NOLINT(build/namespaces)

void
set_install_command(CLI::App* subcom, Configuration& config)
{
    init_install_options(subcom, config);

    auto& freeze_installed = config.at("freeze_installed");
    subcom->add_flag(
        "--freeze-installed",
        freeze_installed.get_cli_config<bool>(),
        freeze_installed.description()
    );

    auto& force_reinstall = config.at("force_reinstall");
    subcom->add_flag(
        "--force-reinstall",
        force_reinstall.get_cli_config<bool>(),
        force_reinstall.description()
    );

    auto& revision = config.insert(
        Configurable("revision", int{ -1 }).group("cli").description("Revert to the specified REVISION.")
    );
    subcom->add_option("revision", revision.get_cli_config<int>(), revision.description());
    // ->option_text("REVISION");

    subcom->callback([&] { return mamba::install(config); });
}
