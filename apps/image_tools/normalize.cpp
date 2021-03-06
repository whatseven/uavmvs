/*
 * Copyright (C) 2016-2018, Nils Moehrle
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms
 * of the BSD 3-Clause license. See the LICENSE.txt file for details.
 */

#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "mve/image_io.h"

#include "util/arguments.h"
#include "util/file_system.h"
#include "util/tokenizer.h"

struct Arguments {
    bool clamp;
    std::string in_image;
    std::string out_image;
    float min;
    float max;
    float eps;
    float no_value;
    std::vector<std::string> images;
};

Arguments parse_args(int argc, char **argv) {
    util::Arguments args;
    args.set_exit_on_error(true);
    args.set_nonopt_maxnum(2);
    args.set_nonopt_minnum(2);
    args.set_usage("Usage: " + std::string(argv[0]) + " [OPTS] IN_IMAGE OUT_IMAGE");
    args.set_description("Normalizes the pixel values.");
    args.add_option('c', "clamp", false, "clamp (instead of remove) outliers");
    args.add_option('e', "epsilon", true, "remove outliers in percent [0.0]");
    args.add_option('i', "ignore", true, "set value to ignore [-1.0]");
    args.add_option('\0', "images", true, "calculate normalization based on these images (comma seperated list)."
        "If no image is given the normalization is calculate from IN_IMAGE");
    args.add_option('\0', "minimum", true, "specify minimum (overrides automatic estimation)");
    args.add_option('\0', "maximum", true, "specify maximum (overrides automatic estimation)");
    args.parse(argc, argv);

    Arguments conf;
    conf.in_image = args.get_nth_nonopt(0);
    conf.out_image = args.get_nth_nonopt(1);
    conf.min = std::numeric_limits<float>::lowest();
    conf.max = std::numeric_limits<float>::max();
    conf.eps = 0.0f;
    conf.clamp = false;
    conf.no_value = -1.0f;

    for (util::ArgResult const* i = args.next_option();
         i != 0; i = args.next_option()) {
        switch (i->opt->sopt) {
        case 'e':
            conf.eps = i->get_arg<float>();
        break;
        case 'c':
            conf.clamp = true;
        break;
        case 'i':
            conf.no_value = i->get_arg<float>();
        break;
        case '\0':
            if (i->opt->lopt == "images") {
                util::Tokenizer t;
                t.split(i->arg, ',');
                conf.images = t;
            } else if (i->opt->lopt == "minimum") {
                conf.min = i->get_arg<float>();
            } else if (i->opt->lopt == "maximum") {
                conf.max = i->get_arg<float>();
            } else {
                throw std::invalid_argument("Invalid option");
            }
        break;
        default:
            throw std::invalid_argument("Invalid option");
        }
    }

    if (conf.eps < 0.0f || conf.eps > 1.0f) {
        throw std::invalid_argument("epsilon is supposed to be in the intervall [0.0, 1.0]");
    }

    if (conf.max < conf.min) {
        throw std::invalid_argument("minimum has to be smaller that maximum");
    }

    if (conf.images.empty()) {
        conf.images.push_back(conf.in_image);
    }

    return conf;
}

int main(int argc, char **argv) {
    Arguments args = parse_args(argc, argv);

    mve::FloatImage::Ptr image_to_normalize;

    std::unordered_map<std::string, mve::FloatImage::Ptr> images_to_load;
    for (std::size_t i = 0; i < args.images.size(); i++){
        mve::FloatImage::Ptr image;
        std::string name = args.images[i];
        images_to_load[name] = image;
    }
    images_to_load[args.in_image] = image_to_normalize;

    std::size_t num_values = 0;
    std::unordered_map<std::string, mve::FloatImage::Ptr>::iterator it;
    for (it = images_to_load.begin(); it != images_to_load.end(); it++){
        mve::FloatImage::Ptr image;
        try {
            image = mve::image::load_pfm_file(it->first);
        } catch (std::exception& e) {
            std::cerr << "Could not load image: "<< e.what() << std::endl;
            std::exit(EXIT_FAILURE);
        }
        it->second = image;

        num_values += image->get_value_amount();
    }

    image_to_normalize = images_to_load[args.in_image];

    std::vector<float> values;
    values.reserve(num_values);

    for (std::size_t i = 0; i < args.images.size(); ++i) {
        mve::FloatImage::Ptr image = images_to_load[args.images[i]];
        for (int j = 0; j < image->get_value_amount(); ++j) {
            float value = image->at(j);

            if (value == args.no_value) continue;

            values.push_back(value);
        }
    }

    std::cout << values.size() << " valid values" << std::endl;
    std::size_t c = (values.size() * args.eps) / 2;

    typedef std::vector<float>::iterator Iter;

    std::pair<Iter, Iter> real_minmax = std::minmax_element(values.begin(), values.end());

    float real_min = *(real_minmax.first);
    float real_max = *(real_minmax.second);

    Iter nth;

    float min = args.min;
    if (min == std::numeric_limits<float>::lowest()) {
        nth = values.begin() + c;
        std::nth_element(values.begin(), nth, values.end(), std::less<float>());
        min = *nth;
    }

    float max = args.max;
    if (max == std::numeric_limits<float>::max()) {
        nth = values.begin() + c;
        std::nth_element(values.begin(), nth, values.end(), std::greater<float>());
        max = *nth;
    }

    float delta = max - min;
    std::cout << "Minimal value: " << real_min << std::endl;
    std::cout << "Maximal value: " << real_max << std::endl;
    std::cout << "Normalizing range " << min << " - " << max << std::endl;

    int num_outliers = 0;
    mve::FloatImage::Ptr image = image_to_normalize;
    for (int i = 0; i < image->get_value_amount(); ++i) {
        float value = image->at(i);

        if (value == args.no_value) continue;

        if (value >= min) {
            if(value <= max) {
                image->at(i) = ((value - min) / delta);
            } else {
                image->at(i) = args.clamp ? 1.0f : args.no_value;
                num_outliers++;
            }
        } else {
            image->at(i) = args.clamp ? 0.0f : args.no_value;
            num_outliers++;
        }
    }

    if (args.clamp) {
        std::cout << "Clamped ";
    } else {
        std::cout << "Removed ";
    }
    std::cout << num_outliers << " outliers" << std::endl;

    mve::image::save_pfm_file(image_to_normalize, args.out_image);
}
