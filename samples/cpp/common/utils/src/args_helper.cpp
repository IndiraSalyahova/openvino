// Copyright (C) 2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

// clang-format off
#include <sys/stat.h>

#include <iostream>

#ifdef _WIN32
#    include "samples/os/windows/w_dirent.h"
#else
#    include <dirent.h>
#endif

#include "openvino/openvino.hpp"

#include "gflags/gflags.h"
#include "samples/args_helper.hpp"
#include "samples/slog.hpp"
// clang-format on

/**
 * @brief Checks input file argument and add it to files vector
 * @param files reference to vector to store file names
 * @param arg file or folder name
 * @return none
 */
void readInputFilesArguments(std::vector<std::string>& files, const std::string& arg) {
    struct stat sb;
    if (stat(arg.c_str(), &sb) != 0) {
        slog::warn << "File " << arg << " cannot be opened!" << slog::endl;
        return;
    }
    if (S_ISDIR(sb.st_mode)) {
        struct CloseDir {
            void operator()(DIR* d) const noexcept {
                if (d) {
                    closedir(d);
                }
            }
        };
        using Dir = std::unique_ptr<DIR, CloseDir>;
        Dir dp(opendir(arg.c_str()));
        if (dp == nullptr) {
            slog::warn << "Directory " << arg << " cannot be opened!" << slog::endl;
            return;
        }

        struct dirent* ep;
        while (nullptr != (ep = readdir(dp.get()))) {
            std::string fileName = ep->d_name;
            if (fileName == "." || fileName == "..")
                continue;
            files.push_back(arg + "/" + ep->d_name);
        }
    } else {
        files.push_back(arg);
    }
}

/**
 * @brief This function find -i key in input args. It's necessary to process multiple values for
 * single key
 * @param files reference to vector
 * @return none.
 */
void parseInputFilesArguments(std::vector<std::string>& files) {
    std::vector<std::string> args = gflags::GetArgvs();
    auto args_it = begin(args);
    const auto is_image_arg = [](const std::string& s) {
        return s == "-i" || s == "--images";
    };
    const auto is_arg = [](const std::string& s) {
        return s.front() == '-';
    };

    while (args_it != args.end()) {
        const auto img_start = std::find_if(args_it, end(args), is_image_arg);
        if (img_start == end(args)) {
            break;
        }
        const auto img_begin = std::next(img_start);
        const auto img_end = std::find_if(img_begin, end(args), is_arg);
        for (auto img = img_begin; img != img_end; ++img) {
            readInputFilesArguments(files, *img);
        }
        args_it = img_end;
    }

    if (files.empty()) {
        return;
    }
    size_t max_files = 20;
    if (files.size() < max_files) {
        slog::info << "Files were added: " << files.size() << slog::endl;
        for (const auto& filePath : files) {
            slog::info << "    " << filePath << slog::endl;
        }
    } else {
        slog::info << "Files were added: " << files.size() << ". Too many to display each of them." << slog::endl;
    }
}

std::vector<std::string> splitStringList(const std::string& str, char delim) {
    if (str.empty())
        return {};

    std::istringstream istr(str);

    std::vector<std::string> result;
    std::string elem;
    while (std::getline(istr, elem, delim)) {
        if (elem.empty()) {
            continue;
        }
        result.emplace_back(std::move(elem));
    }

    return result;
}

std::map<std::string, std::string> parseArgMap(std::string argMap) {
    argMap.erase(std::remove_if(argMap.begin(), argMap.end(), ::isspace), argMap.end());

    const auto pairs = splitStringList(argMap, ',');

    std::map<std::string, std::string> parsedMap;
    for (auto&& pair : pairs) {
        const auto lastDelimPos = pair.find_last_of(':');
        auto key = pair.substr(0, lastDelimPos);
        auto value = pair.substr(lastDelimPos + 1);

        if (lastDelimPos == std::string::npos || key.empty() || value.empty()) {
            throw std::invalid_argument("Invalid key/value pair " + pair + ". Expected <layer_name>:<value>");
        }

        parsedMap[std::move(key)] = std::move(value);
    }

    return parsedMap;
}

using supported_precisions_t = std::unordered_map<std::string, InferenceEngine::Precision>;

InferenceEngine::Precision getPrecision(std::string value, const supported_precisions_t& supported_precisions) {
    std::transform(value.begin(), value.end(), value.begin(), ::toupper);

    const auto precision = supported_precisions.find(value);
    if (precision == supported_precisions.end()) {
        throw std::logic_error("\"" + value + "\"" + " is not a valid precision");
    }

    return precision->second;
}

InferenceEngine::Precision getPrecision(const std::string& value) {
    static const supported_precisions_t supported_precisions = {
        {"FP32", InferenceEngine::Precision::FP32}, {"f32", InferenceEngine::Precision::FP32},
        {"FP16", InferenceEngine::Precision::FP16}, {"f16", InferenceEngine::Precision::FP16},
        {"BF16", InferenceEngine::Precision::BF16}, {"bf16", InferenceEngine::Precision::BF16},
        {"U64", InferenceEngine::Precision::U64},   {"u64", InferenceEngine::Precision::U64},
        {"I64", InferenceEngine::Precision::I64},   {"i64", InferenceEngine::Precision::I64},
        {"U32", InferenceEngine::Precision::U32},   {"u32", InferenceEngine::Precision::U32},
        {"I32", InferenceEngine::Precision::I32},   {"i32", InferenceEngine::Precision::I32},
        {"U16", InferenceEngine::Precision::U16},   {"u16", InferenceEngine::Precision::U16},
        {"I16", InferenceEngine::Precision::I16},   {"i16", InferenceEngine::Precision::I16},
        {"U8", InferenceEngine::Precision::U8},     {"u8", InferenceEngine::Precision::U8},
        {"I8", InferenceEngine::Precision::I8},     {"i8", InferenceEngine::Precision::I8},
        {"BOOL", InferenceEngine::Precision::BOOL}, {"boolean", InferenceEngine::Precision::BOOL},
    };

    return getPrecision(value, supported_precisions);
}

using supported_type_t = std::unordered_map<std::string, ov::element::Type>;
ov::element::Type getType(std::string value, const supported_type_t& supported_precisions) {
    std::transform(value.begin(), value.end(), value.begin(), ::toupper);

    const auto precision = supported_precisions.find(value);
    if (precision == supported_precisions.end()) {
        throw std::logic_error("\"" + value + "\"" + " is not a valid precision");
    }

    return precision->second;
}
ov::element::Type getType(const std::string& value) {
    static const supported_type_t supported_types = {
        {"FP32", ov::element::f32}, {"f32", ov::element::f32},      {"FP16", ov::element::f16},
        {"f16", ov::element::f16},  {"BF16", ov::element::bf16},    {"bf16", ov::element::bf16},
        {"U64", ov::element::u64},  {"u64", ov::element::u64},      {"I64", ov::element::i64},
        {"i64", ov::element::i64},  {"U32", ov::element::u32},      {"u32", ov::element::u32},
        {"I32", ov::element::i32},  {"i32", ov::element::i32},      {"U16", ov::element::u16},
        {"u16", ov::element::u16},  {"I16", ov::element::i16},      {"i16", ov::element::i16},
        {"U8", ov::element::u8},    {"u8", ov::element::u8},        {"I8", ov::element::i8},
        {"i8", ov::element::i8},    {"BOOL", ov::element::boolean}, {"boolean", ov::element::boolean},
    };

    return getType(value, supported_types);
}

namespace {
using supported_layouts_t = std::unordered_map<std::string, InferenceEngine::Layout>;
using matchLayoutToDims_t = std::unordered_map<size_t, size_t>;

InferenceEngine::Layout getLayout(std::string value, const supported_layouts_t& supported_layouts) {
    std::transform(value.begin(), value.end(), value.begin(), ::toupper);

    const auto layout = supported_layouts.find(value);
    if (layout == supported_layouts.end()) {
        throw std::logic_error("\"" + value + "\"" + " is not a valid layout");
    }

    return layout->second;
}

InferenceEngine::Layout getLayout(const std::string& value) {
    static const supported_layouts_t supported_layouts = {
        {"NCDHW", InferenceEngine::Layout::NCDHW},
        {"NDHWC", InferenceEngine::Layout::NDHWC},
        {"NCHW", InferenceEngine::Layout::NCHW},
        {"NHWC", InferenceEngine::Layout::NHWC},
        {"CHW", InferenceEngine::Layout::CHW},
        {"HWC", InferenceEngine::Layout::HWC},
        {"NC", InferenceEngine::Layout::NC},
        {"C", InferenceEngine::Layout::C},
    };

    return getLayout(value, supported_layouts);
}

bool isMatchLayoutToDims(InferenceEngine::Layout layout, size_t dimension) {
    static const matchLayoutToDims_t matchLayoutToDims = {{static_cast<size_t>(InferenceEngine::Layout::NCDHW), 5},
                                                          {static_cast<size_t>(InferenceEngine::Layout::NDHWC), 5},
                                                          {static_cast<size_t>(InferenceEngine::Layout::NCHW), 4},
                                                          {static_cast<size_t>(InferenceEngine::Layout::NHWC), 4},
                                                          {static_cast<size_t>(InferenceEngine::Layout::CHW), 3},
                                                          {static_cast<size_t>(InferenceEngine::Layout::NC), 2},
                                                          {static_cast<size_t>(InferenceEngine::Layout::C), 1}};

    const auto dims = matchLayoutToDims.find(static_cast<size_t>(layout));
    if (dims == matchLayoutToDims.end()) {
        throw std::logic_error("Layout is not valid.");
    }

    return dimension == dims->second;
}

}  // namespace

void printInputAndOutputsInfoShort(const ov::Model& network) {
    std::cout << "Network inputs:" << std::endl;
    for (auto&& param : network.get_parameters()) {
        auto l = param->get_layout();
        std::cout << "    " << param->get_friendly_name() << " : " << param->get_element_type() << " / "
                  << param->get_layout().to_string() << std::endl;
    }
    std::cout << "Network outputs:" << std::endl;
    for (auto&& result : network.get_results()) {
        std::cout << "    " << result->get_friendly_name() << " : " << result->get_element_type() << " / "
                  << result->get_layout().to_string() << std::endl;
    }
}

void printInputAndOutputsInfo(const ov::Model& network) {
    slog::info << "model name: " << network.get_friendly_name() << slog::endl;

    const std::vector<ov::Output<const ov::Node>> inputs = network.inputs();
    for (const ov::Output<const ov::Node> input : inputs) {
        slog::info << "    inputs" << slog::endl;

        const std::string name = input.get_names().empty() ? "NONE" : input.get_any_name();
        slog::info << "        input name: " << name << slog::endl;

        const ov::element::Type type = input.get_element_type();
        slog::info << "        input type: " << type << slog::endl;

        const ov::Shape shape = input.get_shape();
        slog::info << "        input shape: " << shape << slog::endl;
    }

    const std::vector<ov::Output<const ov::Node>> outputs = network.outputs();
    for (const ov::Output<const ov::Node> output : outputs) {
        slog::info << "    outputs" << slog::endl;

        const std::string name = output.get_names().empty() ? "NONE" : output.get_any_name();
        slog::info << "        output name: " << name << slog::endl;

        const ov::element::Type type = output.get_element_type();
        slog::info << "        output type: " << type << slog::endl;

        const ov::Shape shape = output.get_shape();
        slog::info << "        output shape: " << shape << slog::endl;
    }
}

void configurePrePostProcessing(std::shared_ptr<ov::Model>& model,
                                const std::string& ip,
                                const std::string& op,
                                const std::string& iop,
                                const std::string& il,
                                const std::string& ol,
                                const std::string& iol,
                                const std::string& iml,
                                const std::string& oml,
                                const std::string& ioml) {
    auto preprocessor = ov::preprocess::PrePostProcessor(model);
    const auto inputs = model->inputs();
    const auto outputs = model->outputs();
    if (!ip.empty()) {
        auto type = getType(ip);
        for (size_t i = 0; i < inputs.size(); i++) {
            preprocessor.input(i).tensor().set_element_type(type);
        }
    }

    if (!op.empty()) {
        auto type = getType(op);
        for (size_t i = 0; i < outputs.size(); i++) {
            preprocessor.output(i).tensor().set_element_type(type);
        }
    }

    if (!iop.empty()) {
        const auto user_precisions_map = parseArgMap(iop);
        for (auto&& item : user_precisions_map) {
            const auto& tensor_name = item.first;
            const auto type = getType(item.second);

            bool tensorFound = false;
            for (size_t i = 0; i < inputs.size(); i++) {
                if (inputs[i].get_names().count(tensor_name)) {
                    preprocessor.input(i).tensor().set_element_type(type);
                    tensorFound = true;
                    break;
                }
            }
            if (!tensorFound) {
                for (size_t i = 0; i < outputs.size(); i++) {
                    if (outputs[i].get_names().count(tensor_name)) {
                        preprocessor.output(i).tensor().set_element_type(type);
                        tensorFound = true;
                        break;
                    }
                }
            }
            OPENVINO_ASSERT(!tensorFound, "Model doesn't have input/output with tensor name: ", tensor_name);
        }
    }
    if (!il.empty()) {
        for (size_t i = 0; i < inputs.size(); i++) {
            preprocessor.input(i).tensor().set_layout(ov::Layout(il));
        }
    }

    if (!ol.empty()) {
        for (size_t i = 0; i < outputs.size(); i++) {
            preprocessor.output(i).tensor().set_layout(ov::Layout(ol));
        }
    }

    if (!iol.empty()) {
        const auto user_precisions_map = parseArgMap(iol);
        for (auto&& item : user_precisions_map) {
            const auto& tensor_name = item.first;

            bool tensorFound = false;
            for (size_t i = 0; i < inputs.size(); i++) {
                if (inputs[i].get_names().count(tensor_name)) {
                    preprocessor.input(i).tensor().set_layout(ov::Layout(item.second));
                    tensorFound = true;
                    break;
                }
            }
            if (!tensorFound) {
                for (size_t i = 0; i < outputs.size(); i++) {
                    if (outputs[i].get_names().count(tensor_name)) {
                        preprocessor.output(i).tensor().set_layout(ov::Layout(item.second));
                        tensorFound = true;
                        break;
                    }
                }
            }
            OPENVINO_ASSERT(!tensorFound, "Model doesn't have input/output with tensor name: ", tensor_name);
        }
    }

    if (!iml.empty()) {
        for (size_t i = 0; i < inputs.size(); i++) {
            preprocessor.input(i).model().set_layout(ov::Layout(iml));
        }
    }

    if (!oml.empty()) {
        for (size_t i = 0; i < outputs.size(); i++) {
            preprocessor.output(i).model().set_layout(ov::Layout(oml));
        }
    }

    if (!ioml.empty()) {
        const auto user_precisions_map = parseArgMap(ioml);
        for (auto&& item : user_precisions_map) {
            const auto& tensor_name = item.first;

            bool tensorFound = false;
            for (size_t i = 0; i < inputs.size(); i++) {
                if (inputs[i].get_names().count(tensor_name)) {
                    preprocessor.input(i).model().set_layout(ov::Layout(item.second));
                    tensorFound = true;
                    break;
                }
            }
            if (!tensorFound) {
                for (size_t i = 0; i < outputs.size(); i++) {
                    if (outputs[i].get_names().count(tensor_name)) {
                        preprocessor.output(i).model().set_layout(ov::Layout(item.second));
                        tensorFound = true;
                        break;
                    }
                }
            }
            OPENVINO_ASSERT(!tensorFound, "Model doesn't have input/output with tensor name: ", tensor_name);
        }
    }

    model = preprocessor.build();
}

ov::element::Type getPrecision(std::string value,
                               const std::unordered_map<std::string, ov::element::Type>& supported_precisions) {
    std::transform(value.begin(), value.end(), value.begin(), ::toupper);

    const auto precision = supported_precisions.find(value);
    if (precision == supported_precisions.end()) {
        throw std::logic_error("\"" + value + "\"" + " is not a valid precision");
    }

    return precision->second;
}

ov::element::Type getPrecision2(const std::string& value) {
    static const std::unordered_map<std::string, ov::element::Type> supported_precisions = {
        {"FP32", ov::element::f32},
        {"FP16", ov::element::f16},
        {"BF16", ov::element::bf16},
        {"U64", ov::element::u64},
        {"I64", ov::element::i64},
        {"U32", ov::element::u32},
        {"I32", ov::element::i32},
        {"U16", ov::element::u16},
        {"I16", ov::element::i16},
        {"U8", ov::element::u8},
        {"I8", ov::element::i8},
        {"BOOL", ov::element::boolean},
    };

    return getPrecision(value, supported_precisions);
}