#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_set>

#include "array3D.hpp"
#include "image.hpp"
#include "overlapping_wfc.hpp"
#include "rapidxml.hpp"
#include "rapidxml_utils.hpp"
#include "tiling_wfc.hpp"
#include "utils.hpp"
#include "wfc.hpp"

using namespace rapidxml;
using namespace std::string_literals;

/**
 * Get a random seed.
 * This function use random_device on linux, but use the C rand function for
 * other targets. This is because, for instance, windows don't implement
 * random_device non-deterministically.
 */
int get_random_seed()
{
#ifdef __linux__
    return std::random_device()();
#else
    return std::rand();
#endif
}

/**
 * Read the overlapping wfc problem from the xml node.
 */
void read_overlapping_instance(xml_node<>* node)
{
    std::string name = rapidxml::get_attribute(node, "name");
    unsigned N = stoi(rapidxml::get_attribute(node, "N"));
    bool periodic_output = (rapidxml::get_attribute(node, "periodic", "False") == "True");
    bool periodic_input = (rapidxml::get_attribute(node, "periodicInput", "True") == "True");
    bool ground = (stoi(rapidxml::get_attribute(node, "ground", "0")) != 0);
    unsigned symmetry = stoi(rapidxml::get_attribute(node, "symmetry", "8"));
    unsigned screenshots = stoi(rapidxml::get_attribute(node, "screenshots", "2"));
    unsigned width = stoi(rapidxml::get_attribute(node, "width", "48"));
    unsigned height = stoi(rapidxml::get_attribute(node, "height", "48"));

    std::cout << name << " started!" << std::endl;
    // Stop hardcoding samples
    const std::string image_path = RESOURCEDIR + name + ".png";
    std::optional<Array2D<Color>> m = read_image(image_path);
    if (!m.has_value()) {
        throw "Error while loading " + image_path;
    }
    OverlappingWFCOptions options
        = { periodic_input, periodic_output, height, width, symmetry, ground, N };
    for (unsigned i = 0; i < screenshots; i++) {
        for (unsigned test = 0; test < 10; test++) {
            int seed = get_random_seed();
            OverlappingWFC<Color> wfc(*m, options, seed);
            std::optional<Array2D<Color>> success = wfc.run();
            if (success.has_value()) {
                write_image_png(IMGDIR + name + std::to_string(i) + ".png", *success);
                std::cout << name << " finished!" << std::endl;
                break;
            } else {
                std::cout << "failed!" << std::endl;
            }
        }
    }
}

/**
 * Transform a symmetry name into its Symmetry enum
 */
Symmetry to_symmetry(const std::string& symmetry_name)
{
    if (symmetry_name == "X"s) {
        return Symmetry::X;
    }
    if (symmetry_name == "T"s) {
        return Symmetry::T;
    }
    if (symmetry_name == "I"s) {
        return Symmetry::I;
    }
    if (symmetry_name == "L"s) {
        return Symmetry::L;
    }
    if (symmetry_name == "\\"s) {
        return Symmetry::backslash;
    }
    if (symmetry_name == "P"s) {
        return Symmetry::P;
    }
    throw symmetry_name + "is an invalid Symmetry"s;
}

/**
 * Read the names of the tiles in the subset in a tiling WFC problem
 */
std::optional<std::unordered_set<std::string>> read_subset_names(
    xml_node<>* root_node, const std::string& subset)
{
    std::unordered_set<std::string> subset_names;
    xml_node<>* subsets_node = root_node->first_node("subsets"s.c_str());
    if (!subsets_node) {
        return std::nullopt;
    }
    xml_node<>* subset_node = subsets_node->first_node("subset"s.c_str());
    while (subset_node && rapidxml::get_attribute(subset_node, "name"s.c_str()) != subset) {
        subset_node = subset_node->next_sibling("subset"s.c_str());
    }
    if (!subset_node) {
        return std::nullopt;
    }
    for (xml_node<>* node = subset_node->first_node("tile"s.c_str()); node;
         node = node->next_sibling("tile"s.c_str())) {
        subset_names.insert(rapidxml::get_attribute(node, "name"s.c_str()));
    }
    return subset_names;
}

/**
 * Read all tiles for a tiling problem
 */
// std::unordered_map<string, Tile<Color>> read_tiles(
auto read_tiles(xml_node<>* root_node, const std::string& current_dir, const std::string& subset,
    unsigned size)
{
    auto subset_names = read_subset_names(root_node, subset);
    std::unordered_map<std::string, Tile<Color>> tiles;
    xml_node<>* tiles_node = root_node->first_node("tiles"s.c_str());
    for (xml_node<>* node = tiles_node->first_node("tile"s.c_str()); node;
         node = node->next_sibling("tile"s.c_str())) {
        std::string name = rapidxml::get_attribute(node, "name"s.c_str());
        if (subset_names != std::nullopt && subset_names->find(name) == subset_names->end()) {
            continue;
        }
        Symmetry symmetry
            = to_symmetry(rapidxml::get_attribute(node, "symmetry"s.c_str(), "X"s.c_str()));
        double weight = stod(rapidxml::get_attribute(node, "weight"s.c_str(), "1.0"s.c_str()));
        const auto image_path = current_dir + "/"s + name + ".png"s;
        std::optional<Array2D<Color>> image = read_image(image_path);

        if (image == std::nullopt) {
            std::vector<Array2D<Color>> images;
            for (unsigned i = 0; i < nb_of_possible_orientations(symmetry); i++) {
                const std::string image_path
                    = current_dir + "/" + name + " " + std::to_string(i) + ".png";
                std::optional<Array2D<Color>> image = read_image(image_path);
                if (image == std::nullopt) {
                    throw "Error while loading " + image_path;
                }
                if ((image->width != size) || (image->height != size)) {
                    throw "Image " + image_path + " has wrond size";
                }
                images.push_back(*image);
            }
            Tile<Color> tile = { images, symmetry, weight };
            tiles.insert({ name, tile });
        } else {
            if ((image->width != size) || (image->height != size)) {
                throw "Image " + image_path + " has wrong size";
            }

            Tile<Color> tile(*image, symmetry, weight);
            tiles.insert({ name, tile });
        }
    }

    return tiles;
}

/**
 * Read the neighbors constraints for a tiling problem.
 * A value {t1,o1,t2,o2} means that the tile t1 with orientation o1 can be
 * placed at the right of the tile t2 with orientation o2.
 */
auto read_neighbors(xml_node<>* root_node)
{
    std::vector<std::tuple<std::string, unsigned int, std::string, unsigned int>> neighbors;
    xml_node<>* neighbor_node = root_node->first_node("neighbors");
    for (xml_node<>* node = neighbor_node->first_node("neighbor"); node;
         node = node->next_sibling("neighbor")) {
        auto left = rapidxml::get_attribute(node, "left");
        auto left_delimiter = left.find(" ");
        auto left_tile = left.substr(0, left_delimiter);
        unsigned int left_orientation = 0;
        if (left_delimiter != std::string::npos) {
            left_orientation = stoi(left.substr(left_delimiter, std::string::npos));
        }

        auto right = rapidxml::get_attribute(node, "right");
        auto right_delimiter = right.find(" ");
        auto right_tile = right.substr(0, right_delimiter);
        unsigned int right_orientation = 0;
        if (right_delimiter != std::string::npos) {
            right_orientation = stoi(right.substr(right_delimiter, std::string::npos));
        }
        neighbors.push_back({ left_tile, left_orientation, right_tile, right_orientation });
    }
    return neighbors;
}

/**
 * Read an instance of a tiling WFC problem.
 */
void read_simpletiled_instance(xml_node<>* node, const std::string& current_dir) noexcept
{
    auto name = rapidxml::get_attribute(node, "name"s);
    auto subset = rapidxml::get_attribute(node, "subset"s, "tiles"s);
    bool periodic_output = (rapidxml::get_attribute(node, "periodic"s, "False"s) == "True"s);
    unsigned int width = stoi(rapidxml::get_attribute(node, "width"s, "48"s));
    unsigned int height = stoi(rapidxml::get_attribute(node, "height"s, "48"s));

    std::cout << name << " " << subset << " started!"s << std::endl;

    std::ifstream config_file(RESOURCEDIR + name + "/data.xml"s);
    std::vector<char> buffer(
        (std::istreambuf_iterator<char>(config_file)), std::istreambuf_iterator<char>());
    buffer.push_back('\0');
    xml_document<> data_document;
    data_document.parse<0>(&buffer[0]);
    auto data_root_node = data_document.first_node("set"s.c_str());
    unsigned int size = stoi(rapidxml::get_attribute(data_root_node, "size"s.c_str()));

    std::unordered_map<std::string, Tile<Color>> tiles_map
        = read_tiles(data_root_node, current_dir + "/"s + name, subset, size);
    std::unordered_map<std::string, unsigned> tiles_id;
    std::vector<Tile<Color>> tiles;
    unsigned id = 0;
    for (std::pair<std::string, Tile<Color>> tile : tiles_map) {
        tiles_id.insert({ tile.first, id });
        tiles.push_back(tile.second);
        id++;
    }

    auto neighbors = read_neighbors(data_root_node);
    std::vector<std::tuple<unsigned, unsigned, unsigned, unsigned>> neighbors_ids;
    for (auto neighbor : neighbors) {
        const std::string& neighbor1 = std::get<0>(neighbor);
        const int& orientation1 = std::get<1>(neighbor);
        const std::string& neighbor2 = std::get<2>(neighbor);
        const int& orientation2 = std::get<3>(neighbor);
        if (tiles_id.find(neighbor1) == tiles_id.end()) {
            continue;
        }
        if (tiles_id.find(neighbor2) == tiles_id.end()) {
            continue;
        }
        neighbors_ids.push_back(
            std::make_tuple(tiles_id[neighbor1], orientation1, tiles_id[neighbor2], orientation2));
    }

    for (unsigned test = 0; test < 10; test++) {
        int seed = get_random_seed();
        TilingWFC<Color> wfc(tiles, neighbors_ids, height, width, { periodic_output }, seed);
        std::optional<Array2D<Color>> success = wfc.run();
        if (success.has_value()) {
            write_image_png(IMGDIR + name + "_"s + subset + ".png"s, *success);
            std::cout << name << " finished!"s << std::endl;
            break;
        } else {
            std::cout << "failed!"s << std::endl;
        }
    }
}

/**
 * Read a configuration file containing multiple wfc problems
 */
void read_config_file(const std::string& config_path) noexcept
{
    std::cout << "Using this config file: "s << config_path << std::endl;

    std::ifstream config_file(config_path);
    std::vector<char> buffer(
        (std::istreambuf_iterator<char>(config_file)), std::istreambuf_iterator<char>());
    buffer.push_back('\0');
    xml_document<> document;
    document.parse<0>(&buffer[0]);

    auto root_node = document.first_node("samples"s.c_str());
    auto dir_path = get_dir(config_path) + "/"s;

    for (xml_node<>* node = root_node->first_node("overlapping"s.c_str()); node;
         node = node->next_sibling("overlapping"s.c_str())) {
        read_overlapping_instance(node);
    }
    for (xml_node<>* node = root_node->first_node("simpletiled"s.c_str()); node;
         node = node->next_sibling("simpletiled"s.c_str())) {
        read_simpletiled_instance(node, dir_path);
    }
}

int main()
{

// Initialize rand for non-linux targets
#ifndef __linux__
    srand(time(nullptr));
#endif

    auto start = std::chrono::system_clock::now();
    read_config_file(RESOURCEDIR "samples.xml"s);
    auto end = std::chrono::system_clock::now();

    int elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    int elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "All samples done in "s << elapsed_s << "s, "s << elapsed_ms % 1000 << "ms.\n"s;
}
