#pragma once

#include <cblas.h>
#include <lapacke.h>
#include <stddef.h>
#include <stdlib.h>
#include <cstdlib>
#include "commons.hpp"

using data_type = double;

// generate positive definition matrix
template <typename T>
using Matrix = std::vector<std::vector<T>>;

template <typename T>
std::vector<T> generate_pascal_matrix(const int n) {
    Matrix<T> matrix(n, std::vector<T>(n, static_cast<T>(0)));

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (i == 0 || j == 0) {
                matrix[i][j] = static_cast<T>(1);
            } else {
                matrix[i][j] = matrix[i][j - 1] + matrix[i - 1][j];
            }
        }
    }

    std::vector<T> flattenedVector;
    for (const auto& row : matrix) {
        flattenedVector.insert(flattenedVector.end(), row.begin(), row.end());
    }
    return std::move(flattenedVector);
}

// parameters define
struct args_params_t : public argparse::Args {
    std::uint64_t& mat_size = kwarg("mat_size", "size of input matrix_size").set_default(10);
    std::uint64_t& num_tiles = kwarg("num_tiles", "number of tiles").set_default(2);
    int& verifycorrectness =
        kwarg("verifycorrectness", "verify the tiled_cholesky results with LAPACKE_dpotrf cholesky").set_default(1);
    bool& lower_matrix = kwarg("lower_matrix", "print generated results (default: false)").set_default(true);
    bool& help = flag("h, help", "print help");
    bool& time = kwarg("t, time", "print time").set_default(true);
};

// help functions for tiled_cholesky
data_type* generate_positiveDefinitionMatrix(const size_t matrix_size) {
    data_type* A_matrix = new data_type[matrix_size * matrix_size];
    data_type* pd_matrix = (data_type*)malloc(matrix_size * matrix_size * sizeof(data_type));
    unsigned int seeds = matrix_size;

    // generate a random symmetric matrix
    for (size_t row = 0; row < matrix_size; ++row) {
        for (size_t col = row; col < matrix_size; ++col) {
            A_matrix[col * matrix_size + row] = A_matrix[row * matrix_size + col] =
                (data_type)rand_r(&seeds) / RAND_MAX;
        }
    }
    // compute the product of matrix A_matrix and its transpose, and storing the result in pd_matrix.
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasTrans, matrix_size, matrix_size, matrix_size, 1.0, A_matrix,
                matrix_size, A_matrix, matrix_size, 0.0, pd_matrix, matrix_size);

    // Adjust Diagonals
    for (size_t row = 0; row < matrix_size; ++row) {
        double diagonals = 1.0;  // from 1.0
        for (size_t col = 0; col < matrix_size; ++col) {
            diagonals += pd_matrix[row * matrix_size + col];
        }
        // Set the diag entry
        pd_matrix[row * matrix_size + row] = diagonals;
    }

    delete[] A_matrix;
    return pd_matrix;
}

void split_into_tiles(const data_type* matrix, data_type* matrix_split[], const int num_tiles, const int tile_size,
                      const int size, bool layRow) {

    int total_num_tiles = num_tiles * num_tiles;
    int offset_tile;

    //#pragma omp parallel for private(i, j, offset_tile) schedule(auto)
    for (int i_tile = 0; i_tile < total_num_tiles; ++i_tile) {
        if (layRow) {
            offset_tile =
                int(i_tile / num_tiles) * num_tiles * tile_size * tile_size + int(i_tile % num_tiles) * tile_size;
        } else {
            offset_tile =
                int(i_tile % num_tiles) * num_tiles * tile_size * tile_size + int(i_tile / num_tiles) * tile_size;
        }

        for (int i = 0; i < tile_size; ++i)
            //#pragma simd
            for (int j = 0; j < tile_size; ++j) {
                matrix_split[i_tile][i * tile_size + j] = matrix[offset_tile + i * size + j];
            }
    }
}

void assemble_tiles(data_type* matrix_split[], data_type* matrix, const int num_tiles, const int tile_size,
                    const int size, bool layRow) {
    int i_tile, j_tile, tile, i_local, j_local;
    //#pragma omp parallel for private(j, i_local, j_local, i_tile, j_tile, tile) \
    schedule(auto)
    for (int i = 0; i < size; ++i) {
        i_local = int(i % tile_size);
        i_tile = int(i / tile_size);
        //#pragma simd private(j_tile, tile, j_local)
        for (int j = 0; j < size; ++j) {
            j_tile = int(j / tile_size);
            if (layRow) {
                tile = i_tile * num_tiles + j_tile;
            } else {
                tile = j_tile * num_tiles + i_tile;
            }
            j_local = int(j % tile_size);
            matrix[i * size + j] = matrix_split[tile][i_local * tile_size + j_local];
        }
    }
}

bool verify_results(const data_type* lower_res, const data_type* dporft_res, const int totalsize) {
    bool res = true;
    data_type diff;
    for (int i = 0; i < totalsize; ++i) {
        diff = dporft_res[i] - lower_res[i];
        if (fabs(dporft_res[i]) > 1e-5) {
            diff /= dporft_res[i];
        }
        diff = fabs(diff);
        if (diff > 1.0e-5) {
            fmt::print("\nError detected at i = {}: ref {} actual {}\n", i, dporft_res[i], lower_res[i]);
            res = false;
            break;
        }
    }
    return res;
}

void printLowerResults(const data_type* matrix, size_t matrix_size) {
    for (size_t row = 0; row < matrix_size; ++row) {
        for (size_t col = 0; col <= row; ++col) {
            fmt::print("{}\t", matrix[row * matrix_size + col]);
        }
        fmt::print("\n");
    }
}

void print_mat_split(data_type* matrix_split[], int num_tiles, int tile_size) {
    for (int itile = 0; itile < num_tiles * num_tiles; ++itile) {
        fmt::print("Block {}:\n", itile);
        for (int i = 0; i < tile_size; ++i) {
            for (int j = 0; j < tile_size; ++j) {
                fmt::print("{} ", matrix_split[itile][i * tile_size + j]);
            }
            fmt::print("\n");
        }
        fmt::print("\n");
    }
}