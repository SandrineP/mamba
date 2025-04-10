// Copyright (c) 2019, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include <catch2/catch_all.hpp>
#include <yaml-cpp/exceptions.h>

#include "mamba/core/channel_context.hpp"
#include "mamba/core/env_lockfile.hpp"
#include "mamba/core/fsutil.hpp"
#include "mamba/core/package_database_loader.hpp"
#include "mamba/core/transaction.hpp"
#include "mamba/solver/libsolv/database.hpp"

#include "mambatests.hpp"

namespace mamba
{
    namespace
    {
        TEST_CASE("absent_file_fails")
        {
            const auto maybe_lockfile = read_environment_lockfile("this/file/does/not/exists");
            REQUIRE_FALSE(maybe_lockfile);
            const auto error = maybe_lockfile.error();
            REQUIRE(mamba_error_code::env_lockfile_parsing_failed == error.error_code());

            const auto& error_details = EnvLockFileError::get_details(error);
            REQUIRE(file_parsing_error_code::parsing_failure == error_details.parsing_error_code);
            REQUIRE(error_details.yaml_error_type);
            const std::type_index bad_file_error_id{ typeid(YAML::BadFile) };
            REQUIRE(bad_file_error_id == error_details.yaml_error_type.value());

            // NOTE: one could attempt to check if opening a file which is not an YAML file
            //       would fail. Unfortunately YAML parsers will accept any kind of file,
            //       and assume it is YAML or at worse a comment or raw string. So there
            //       is no good way to check that.
        }

        TEST_CASE("invalid_version_fails")
        {
            const fs::u8path invalid_version_lockfile_path{ mambatests::test_data_dir
                                                            / "env_lockfile/bad_version-lock.yaml" };
            const auto maybe_lockfile = read_environment_lockfile(invalid_version_lockfile_path);
            REQUIRE_FALSE(maybe_lockfile);
            const auto error = maybe_lockfile.error();
            REQUIRE(mamba_error_code::env_lockfile_parsing_failed == error.error_code());
            const auto& error_details = EnvLockFileError::get_details(error);
            REQUIRE(file_parsing_error_code::unsupported_version == error_details.parsing_error_code);
        }

        TEST_CASE("valid_no_package_succeed")
        {
            const fs::u8path lockfile_path{ mambatests::test_data_dir
                                            / "env_lockfile/good_no_package-lock.yaml" };
            const auto maybe_lockfile = read_environment_lockfile(lockfile_path);
            if (!maybe_lockfile)
            {
                INFO(maybe_lockfile.error().what());
                FAIL();
            }
            const auto lockfile = maybe_lockfile.value();
            REQUIRE(lockfile.get_all_packages().empty());
        }

        TEST_CASE("invalid_package_fails")
        {
            const fs::u8path lockfile_path{ mambatests::test_data_dir
                                            / "env_lockfile/bad_package-lock.yaml" };
            const auto maybe_lockfile = read_environment_lockfile(lockfile_path);
            REQUIRE_FALSE(maybe_lockfile);
            const auto error = maybe_lockfile.error();
            REQUIRE(mamba_error_code::env_lockfile_parsing_failed == error.error_code());
            const auto& error_details = EnvLockFileError::get_details(error);
            REQUIRE(file_parsing_error_code::parsing_failure == error_details.parsing_error_code);
        }

        TEST_CASE("valid_one_package_succeed")
        {
            const fs::u8path lockfile_path{ mambatests::test_data_dir
                                            / "env_lockfile/good_one_package-lock.yaml" };
            const auto maybe_lockfile = read_environment_lockfile(lockfile_path);
            if (!maybe_lockfile)
            {
                INFO(maybe_lockfile.error().what());
                FAIL();
            }
            const auto lockfile = maybe_lockfile.value();
            REQUIRE(lockfile.get_all_packages().size() == 1);
        }

        TEST_CASE("valid_one_package_implicit_category")
        {
            const fs::u8path lockfile_path{
                mambatests::test_data_dir / "env_lockfile/good_one_package_missing_category-lock.yaml"
            };
            const auto maybe_lockfile = read_environment_lockfile(lockfile_path);
            if (!maybe_lockfile)
            {
                INFO(maybe_lockfile.error().what());
                FAIL();
            }
            const auto lockfile = maybe_lockfile.value();
            REQUIRE(lockfile.get_all_packages().size() == 1);
        }

        TEST_CASE("valid_multiple_packages_succeed")
        {
            const fs::u8path lockfile_path{ mambatests::test_data_dir
                                            / "env_lockfile/good_multiple_packages-lock.yaml" };
            const auto maybe_lockfile = read_environment_lockfile(lockfile_path);
            if (!maybe_lockfile)
            {
                INFO(maybe_lockfile.error().what());
                FAIL();
            }
            const auto lockfile = maybe_lockfile.value();
            REQUIRE(lockfile.get_all_packages().size() > 1);
        }

        TEST_CASE("get_specific_packages")
        {
            const fs::u8path lockfile_path{ mambatests::test_data_dir
                                            / "env_lockfile/good_multiple_packages-lock.yaml" };
            const auto lockfile = read_environment_lockfile(lockfile_path).value();
            REQUIRE(lockfile.get_packages_for("", "", "").empty());
            {
                const auto packages = lockfile.get_packages_for("main", "linux-64", "conda");
                REQUIRE_FALSE(packages.empty());
                REQUIRE(packages.size() > 4);
            }
            {
                const auto packages = lockfile.get_packages_for("main", "linux-64", "pip");
                REQUIRE_FALSE(packages.empty());
                REQUIRE(packages.size() == 2);
            }
        }

        TEST_CASE("create_transaction_with_categories")
        {
            auto& ctx = mambatests::context();
            const fs::u8path lockfile_path{ mambatests::test_data_dir
                                            / "env_lockfile/good_multiple_categories-lock.yaml" };
            auto channel_context = ChannelContext::make_conda_compatible(mambatests::context());
            solver::libsolv::Database db{ channel_context.params() };
            add_spdlog_logger_to_database(db);
            mamba::MultiPackageCache pkg_cache({ "/tmp/" }, ctx.validation_params);

            ctx.platform = "linux-64";

            auto check_categories =
                [&](std::vector<std::string> categories, size_t num_conda, size_t num_pip)
            {
                std::vector<detail::other_pkg_mgr_spec> other_specs;
                auto transaction = create_explicit_transaction_from_lockfile(
                    ctx,
                    db,
                    lockfile_path,
                    categories,
                    pkg_cache,
                    other_specs
                );
                auto to_install = std::get<1>(transaction.to_conda());
                REQUIRE(to_install.size() == num_conda);
                if (num_pip == 0)
                {
                    REQUIRE(other_specs.size() == 0);
                }
                else
                {
                    REQUIRE(other_specs.size() == 1);
                    REQUIRE(other_specs.at(0).deps.size() == num_pip);
                }
            };

            check_categories({ "main" }, 3, 5);
            check_categories({ "main", "dev" }, 31, 6);
            check_categories({ "dev" }, 28, 1);
            check_categories({ "nonesuch" }, 0, 0);

            ctx.platform = ctx.host_platform;
        }
    }

    namespace
    {
        TEST_CASE("is_env_lockfile_name")
        {
            REQUIRE(is_env_lockfile_name("something-lock.yaml"));
            REQUIRE(is_env_lockfile_name("something-lock.yml"));
            REQUIRE(is_env_lockfile_name("/some/dir/something-lock.yaml"));
            REQUIRE(is_env_lockfile_name("/some/dir/something-lock.yml"));
            REQUIRE(is_env_lockfile_name("../../some/dir/something-lock.yaml"));
            REQUIRE(is_env_lockfile_name("../../some/dir/something-lock.yml"));

            REQUIRE(is_env_lockfile_name(fs::u8path{ "something-lock.yaml" }.string()));
            REQUIRE(is_env_lockfile_name(fs::u8path{ "something-lock.yml" }.string()));
            REQUIRE(is_env_lockfile_name(fs::u8path{ "/some/dir/something-lock.yaml" }.string()));
            REQUIRE(is_env_lockfile_name(fs::u8path{ "/some/dir/something-lock.yml" }.string()));
            REQUIRE(is_env_lockfile_name(fs::u8path{ "../../some/dir/something-lock.yaml" }.string()));
            REQUIRE(is_env_lockfile_name(fs::u8path{ "../../some/dir/something-lock.yml" }.string()));

            REQUIRE_FALSE(is_env_lockfile_name("something"));
            REQUIRE_FALSE(is_env_lockfile_name("something-lock"));
            REQUIRE_FALSE(is_env_lockfile_name("/some/dir/something"));
            REQUIRE_FALSE(is_env_lockfile_name("../../some/dir/something"));

            REQUIRE_FALSE(is_env_lockfile_name("something.yaml"));
            REQUIRE_FALSE(is_env_lockfile_name("something.yml"));
            REQUIRE_FALSE(is_env_lockfile_name("/some/dir/something.yaml"));
            REQUIRE_FALSE(is_env_lockfile_name("/some/dir/something.yml"));
            REQUIRE_FALSE(is_env_lockfile_name("../../some/dir/something.yaml"));
            REQUIRE_FALSE(is_env_lockfile_name("../../some/dir/something.yml"));

            REQUIRE_FALSE(is_env_lockfile_name(fs::u8path{ "something" }.string()));
            REQUIRE_FALSE(is_env_lockfile_name(fs::u8path{ "something-lock" }.string()));
            REQUIRE_FALSE(is_env_lockfile_name(fs::u8path{ "/some/dir/something" }.string()));
            REQUIRE_FALSE(is_env_lockfile_name(fs::u8path{ "../../some/dir/something" }.string()));
        }
    }

}
