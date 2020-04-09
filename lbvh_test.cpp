#include <lbvh.h>

#include "models/model.h"

#include <chrono>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using namespace lbvh;

//! \brief Calculates the volume of a bounding box.
//! This is used to compare the volume of bounding
//! boxes, between the parent and sub nodes.
float volume_of(const aabb<float>& box) noexcept {
  auto size = detail::size_of(box);
  return size.x * size.y * size.z;
}

//! \brief This function traverses the box and ensures that every
//! sub node has a box volume that's less than its parent.
//!
//! \param bvh The bvh to check.
//!
//! \param errors_fatal Whether or not the function should exit
//! at the first occurence of an error.
//!
//! \param index The index of the node to check. Since this is
//! a recursive function, this parameter is only set on recursive calls.
int check_bvh_volumes(const bvh<float>& bvh, int errors_fatal, size_type index = 0) {

  const auto& node = bvh.at(index);

  auto parent_volume = volume_of(node.box);

  int errors = 0;

  if (!node.left_is_leaf()) {
    auto left_volume = volume_of(bvh.at(node.left).box);
    if (parent_volume < left_volume) {
      std::printf("Parent node %lu volume is less than left sub node %u\n", index, node.left);
      std::printf("  Parent node volume : %8.04f\n", parent_volume);
      std::printf("  Sub node volume    : %8.04f\n", left_volume);
      errors++;
    }
  }

  if (!node.right_is_leaf()) {
    auto right_volume = volume_of(bvh.at(node.right).box);
    if (parent_volume < right_volume) {
      std::printf("Parent node %lu volume is less than right sub node %u\n", index, node.right);
      std::printf("  Parent node volume : %8.04f\n", parent_volume);
      std::printf("  Sub node volume    : %8.04f\n", right_volume);
      errors++;
    }
  }

  if (errors && errors_fatal) {
    return EXIT_FAILURE;
  }

  int exit_code = errors ? EXIT_FAILURE : EXIT_SUCCESS;

  if (!node.left_is_leaf()) {
    int ret = check_bvh_volumes(bvh, errors_fatal, node.left);
    if (ret != EXIT_SUCCESS) {
      if (errors_fatal) {
        return ret;
      } else {
        exit_code = ret;
      }
    }
  }

  if (!node.right_is_leaf()) {
    auto ret = check_bvh_volumes(bvh, errors_fatal, node.right);
    if (ret != EXIT_SUCCESS) {
      exit_code = ret;
    }
  }

  return exit_code;
}

//! \brief This function validates the BVH that was built,
//! ensuring that all leafs get referenced once and all nodes
//! other than the root node get referenced once as well.
//!
//! \param bvh The BVH to validate.
//!
//! \param errors_fatal If non-zero, the first error causes
//! the function to return .
//!
//! \return If non-zero, an error was found within the BVH.
int check_bvh(const bvh<float>& bvh, int errors_fatal) {

  int errors = 0;

  std::vector<size_type> node_counts(bvh.size());

  for (size_type i = 0; i < bvh.size(); i++) {

    if (!bvh[i].left_is_leaf()) {
      node_counts.at(bvh[i].left)++;
    }

    if (!bvh[i].right_is_leaf()) {
      node_counts.at(bvh[i].right)++;
    }
  }

  if (node_counts[0] > 0) {
    std::printf("%s:%d: Root node was referenced %lu times.\n", __FILE__, __LINE__, node_counts[0]);
    if (errors_fatal) {
      return EXIT_FAILURE;
    }
  }

  for (size_type i = 1; i < node_counts.size(); i++) {

    auto n = node_counts[i];

    if (n != 1) {
      std::printf("%s:%d: Node %lu was counted %lu times.\n", __FILE__, __LINE__, i, n);
      if (errors_fatal) {
        return EXIT_FAILURE;
      } else {
        errors++;
      }
    }
  }

  std::vector<size_type> leaf_counts(bvh.size() + 1);

  for (size_type i = 0; i < bvh.size() + 1; i++) {

    if (bvh[i].left_is_leaf()) {
      leaf_counts.at(bvh[i].left_leaf_index())++;
    }

    if (bvh[i].right_is_leaf()) {
      leaf_counts.at(bvh[i].right_leaf_index())++;
    }
  }

  for (size_type i = 0; i < bvh.size() + 1; i++) {
    auto n = leaf_counts[i];
    if (n != 1) {
      std::printf("%s:%d: Leaf %lu was referenced %lu times.\n", __FILE__, __LINE__, i, n);
      if (errors_fatal) {
        return EXIT_FAILURE;
      } else {
        errors++;
      }
    }
  }

  if (errors) {
    return EXIT_FAILURE;
  } else {
    return check_bvh_volumes(bvh, errors_fatal);
  }
}

//! Represents a simple RGB color.
template <typename scalar_type>
struct color final {
  //! The red channel value.
  scalar_type r;
  //! The green channel value.
  scalar_type g;
  //! The blue channel value.
  scalar_type b;
};

//! \brief This class is used for generating rays for the
//! test traversal.
template <typename scalar_type>
class ray_scheduler final {
  //! The X resolution of the image to produce.
  size_type x_res;
  //! The Y resolution of the image to produce.
  size_type y_res;
public:
  //! Constructs a new instance of the ray scheduler.
  ray_scheduler(size_type width, size_type height) : x_res(width), y_res(height) { }
  //! Executes a kernel across all rays generated from the camera.
  //!
  //! \param kern The ray tracing kernel to pass the rays to.
  //!
  //! \return An image buffer containing the resultant data.
  //! The resolution of the buffer is specified by the width
  //! and height parameters given by the constructor.
  template <typename trace_kernel>
  auto make_frame(trace_kernel kern) {

    //! The image buffer to plot the colors to.
    std::vector<unsigned char> image_buf(x_res * y_res * 3);

    auto* pixels = image_buf.data();

    auto aspect_ratio = scalar_type(x_res) / y_res;

    auto fov = 0.75f;

    for (size_type y = 0; y < y_res; y++) {

      for (size_type x = 0; x < x_res; x++) {

        auto x_ndc =  (2 * (x + scalar_type(0.5)) / scalar_type(x_res)) - 1;
        auto y_ndc = -(2 * (y + scalar_type(0.5)) / scalar_type(y_res)) + 1;

        vec3<scalar_type> ray_dir {
          x_ndc * aspect_ratio * fov,
          y_ndc * fov,
          -1
        };

        ray<scalar_type> r({ 0, 0, 5 }, ray_dir);

        auto color = kern(r);

        pixels[0] = color.r * 255;
        pixels[1] = color.g * 255;
        pixels[2] = color.b * 255;

        pixels += 3;
      }
    }

    return image_buf;
  }
};

//! Runs the test program with a specified floating point type.
//!
//! \tparam scalar_type The floating point type to be used.
//!
//! \param filename The path to the .obj file to render.
//!
//! \param errors_fatal Whether or not to exit on the first error.
//!
//! \return An exit code suitable for the return value of "main."
template <typename scalar_type>
int run_test(const char* filename, int errors_fatal) {

  lbvh::model<scalar_type> model;

  std::printf("Loading model: %s\n", filename);

  model.load(filename);

  std::printf("Model loaded\n");

  std::printf("Building BVH\n");

  lbvh::model_aabb_converter<scalar_type> aabb_converter(model);

  lbvh::model_intersector<scalar_type> intersector(model);

  auto face_indices = model.get_face_indices();

  lbvh::builder<scalar_type> builder;

  auto start = std::chrono::high_resolution_clock::now();

  auto bvh = builder(face_indices.data(), face_indices.size(), aabb_converter);

  auto stop = std::chrono::high_resolution_clock::now();

  auto micro_seconds = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();

  auto milli_seconds = micro_seconds / 1000.0;

  std::printf("  Completed in %6.03f ms.\n", milli_seconds);

  std::printf("Checking BVH\n");

  auto result = check_bvh(bvh, errors_fatal);
  if (result != EXIT_SUCCESS) {
    return result;
  }

  std::printf("  Awesomeness! It works.\n");

  traverser<scalar_type, size_type> traverser(bvh, face_indices.data());

  auto tracer_kern = [&traverser, &intersector](const ray<scalar_type>& r) {

    auto isect = traverser(r, intersector);

    return color<scalar_type> {
      isect.uv.x,
      isect.uv.y,
      0.5
    };
  };

  ray_scheduler<scalar_type> scheduler(1000, 1000);

  auto image = scheduler.make_frame(tracer_kern);

  return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {

  int errors_fatal = 0;

  const char* filename = "models/sponza.obj";

  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--errors_fatal") == 0) {
      errors_fatal = 1;
    } else if (argv[i][0] != '-') {
      filename = argv[i];
    } else {
      std::fprintf(stderr, "Unknown option '%s'\n", argv[i]);
      return EXIT_FAILURE;
    }
  }

  int result = run_test<float>(filename, errors_fatal);

  return result;
}
