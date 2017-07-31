package(default_visibility = ["//visibility:public"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_proto_library",
)

envoy_cc_binary(
    name = "envoy",
    repository = "@envoy",
    deps = [
        ":echo2_config",
        ":inject_config",
        "@envoy//source/exe:envoy_main_entry_lib",
    ],
)

envoy_cc_library(
    name = "echo2_lib",
    srcs = ["echo2.cc"],
    hdrs = ["echo2.h"],
    repository = "@envoy",
    deps = [
        "@envoy//include/envoy/buffer:buffer_interface",
        "@envoy//include/envoy/network:connection_interface",
        "@envoy//include/envoy/network:filter_interface",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/common/common:logger_lib",
    ],
)

envoy_cc_library(
    name = "echo2_config",
    srcs = ["echo2_config.cc"],
    repository = "@envoy",
    deps = [
        ":echo2_lib",
        "@envoy//include/envoy/network:filter_interface",
        "@envoy//include/envoy/registry:registry",
        "@envoy//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_test(
    name = "echo2_integration_test",
    srcs = ["echo2_integration_test.cc"],
    data =  ["echo2_server.json"],
    repository = "@envoy",
    deps = [
        ":echo2_config",
        "@envoy//test/integration:integration_lib"
    ],
)

envoy_proto_library(
    name = "inject_proto",
    srcs = ["inject.proto"],
)

envoy_cc_library(
    name = "inject_lib",
    srcs = ["inject.cc"],
    hdrs = ["inject.h"],
    repository = "@envoy",
    deps = [
        ":inject_proto",
        "@envoy//source/common/grpc:async_client_lib",
        "@envoy//include/envoy/http:filter_interface",
        "@envoy//include/envoy/local_info:local_info_interface",
        "@envoy//include/envoy/runtime:runtime_interface",
        "@envoy//include/envoy/upstream:cluster_manager_interface",
        "@envoy//include/envoy/http:header_map_interface",
        "@envoy//source/common/http:header_map_lib",
        "@envoy//source/common/http:utility_lib",
        "@envoy//source/common/common:utility_lib",
        "@envoy//source/common/json:config_schemas_lib",
        "@envoy//source/common/json:json_loader_lib",
        "@envoy//source/common/json:json_validator_lib",
        "@envoy//source/common/common:logger_lib",
    ],
)

envoy_cc_library(
    name = "inject_config",
    srcs = ["inject_config.cc"],
    repository = "@envoy",
    deps = [
        ":inject_lib",
        "@envoy//include/envoy/network:filter_interface",
        "@envoy//include/envoy/registry:registry",
        "@envoy//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_test(
    name = "inject_integration_test",
    srcs = ["inject_integration_test.cc"],
    data =  ["inject_server2.json"],
    repository = "@envoy",
    deps = [
        ":inject_config",
        "@envoy//source/common/json:config_schemas_lib",
        "@envoy//source/common/json:json_loader_lib",
        "@envoy//source/common/json:json_validator_lib",
        "@envoy//test/integration:integration_lib"
    ],
)


sh_test(
    name = "envoy_binary_test",
    srcs = ["envoy_binary_test.sh"],
    data = [":envoy"],
)
