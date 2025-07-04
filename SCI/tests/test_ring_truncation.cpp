/*
Authors: Mayank Rathee
Copyright:
Copyright (c) 2021 Microsoft Research
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <iostream>

#include "BuildingBlocks/truncation.h"

using namespace sci;
using namespace std;

int dim = 1 << 8;
int bw = 41;
int shift = 12;
int shift_bnd = 17;

uint64_t mask_bw = (bw == 64 ? -1 : ((1ULL << bw) - 1));
uint64_t mask_shift = (shift == 64 ? -1 : ((1ULL << shift) - 1));
uint64_t mask_out = ((bw - shift) == 64 ? -1 : ((1ULL << (bw - shift)) - 1));

// vars
int party, port = 32000;
string address = "127.0.0.1";
NetIO *io;
OTPack<NetIO> *otpack;
Truncation *trunc_oracle;
PRG128 prg;

void trunc_reduce() {
    uint64_t *inA = new uint64_t[dim];
    uint64_t *outB = new uint64_t[dim];

    prg.random_data(inA, dim * sizeof(uint64_t));

    for (int i = 0; i < dim; i++) {
        inA[i] &= mask_bw;
        outB[i] = 0;
    }

    trunc_oracle->truncate_and_reduce(dim, inA, outB, shift, bw);

    if (party == ALICE) {
        uint64_t *inA_bob = new uint64_t[dim];
        uint64_t *outB_bob = new uint64_t[dim];
        io->recv_data(inA_bob, sizeof(uint64_t) * dim);
        io->recv_data(outB_bob, sizeof(uint64_t) * dim);
        for (int i = 0; i < dim; i++) {
            inA[i] = (inA[i] + inA_bob[i]) & mask_bw;
            outB[i] = (outB[i] + outB_bob[i]) & mask_out;
        }
        cout << "Testing for correctness..." << endl;
        for (int i = 0; i < dim; i++) {
            assert((inA[i] >> shift) == outB[i]);
        }
        cout << "Correct!" << endl;
    } else {  // BOB
        io->send_data(inA, sizeof(uint64_t) * dim);
        io->send_data(outB, sizeof(uint64_t) * dim);
    }
}

void trunc(bool signed_arithmetic = true) {
    uint64_t *inA = new uint64_t[dim];
    uint64_t *outB = new uint64_t[dim];

    prg.random_data(inA, dim * sizeof(uint64_t));

    for (int i = 0; i < dim; i++) {
        inA[i] &= mask_bw;
        outB[i] = 0;
    }

    int64_t c0 = trunc_oracle->io->counter;
    trunc_oracle->truncate(
        dim, inA, outB, shift, bw, signed_arithmetic, nullptr
    );
    int64_t c1 = trunc_oracle->io->counter;
    std::cout << "truncate " << (signed_arithmetic ? "signed" : "nonsigned")
              << " " << bw << " bits ints ";
    std::cout << "by " << shift << " bits. Sent " << ((c1 - c0) * 8 / dim)
              << "bits\n";

    if (party == ALICE) {
        uint64_t *inA_bob = new uint64_t[dim];
        uint64_t *outB_bob = new uint64_t[dim];
        io->recv_data(inA_bob, sizeof(uint64_t) * dim);
        io->recv_data(outB_bob, sizeof(uint64_t) * dim);
        for (int i = 0; i < dim; i++) {
            inA[i] = (inA[i] + inA_bob[i]) & mask_bw;
            outB[i] = (outB[i] + outB_bob[i]) & mask_bw;
        }
        cout << "Testing for correctness..." << endl;
        for (int i = 0; i < dim; i++) {
            if (signed_arithmetic) {
                assert(
                    (signed_val(inA[i], bw) >> shift) == signed_val(outB[i], bw)
                );
            } else {
                assert((inA[i] >> shift) == outB[i]);
            }
        }
        cout << "Correct!" << endl;
    } else {  // BOB
        io->send_data(inA, sizeof(uint64_t) * dim);
        io->send_data(outB, sizeof(uint64_t) * dim);
    }
}

void div_pow2(bool signed_arithmetic = true) {
    uint64_t *inA = new uint64_t[dim];
    uint64_t *outB = new uint64_t[dim];

    prg.random_data(inA, dim * sizeof(uint64_t));

    for (int i = 0; i < dim; i++) {
        inA[i] &= mask_bw;
        outB[i] = 0;
    }

    trunc_oracle->div_pow2(
        dim, inA, outB, shift, bw, signed_arithmetic, nullptr
    );

    if (party == ALICE) {
        uint64_t *inA_bob = new uint64_t[dim];
        uint64_t *outB_bob = new uint64_t[dim];
        io->recv_data(inA_bob, sizeof(uint64_t) * dim);
        io->recv_data(outB_bob, sizeof(uint64_t) * dim);
        for (int i = 0; i < dim; i++) {
            inA[i] = (inA[i] + inA_bob[i]) & mask_bw;
            outB[i] = (outB[i] + outB_bob[i]) & mask_bw;
        }
        cout << "Testing for correctness..." << endl;
        for (int i = 0; i < dim; i++) {
            if (signed_arithmetic) {
                assert(
                    (signed_val(inA[i], bw) / (1LL << shift)) ==
                    signed_val(outB[i], bw)
                );
            } else {
                assert((inA[i] / (1ULL << shift)) == outB[i]);
            }
        }
        cout << "Correct!" << endl;
    } else {  // BOB
        io->send_data(inA, sizeof(uint64_t) * dim);
        io->send_data(outB, sizeof(uint64_t) * dim);
    }
}

int main(int argc, char **argv) {
    ArgMapping amap;
    amap.arg("r", party, "Role of party: ALICE = 1; BOB = 2");
    amap.arg("p", port, "Port Number");
    amap.arg("N", dim, "Number of ReLU operations");
    amap.arg("l", bw, "Bitlength of inputs");
    amap.arg("s", shift, "Bitlength of shift");
    amap.arg("ip", address, "IP Address of server (ALICE)");

    amap.parse(argc, argv);

    io = new NetIO(party == 1 ? nullptr : address.c_str(), port);
    otpack = new OTPack<NetIO>(io, party);
    trunc_oracle = new Truncation(party, io, otpack);

    cout << "<><><><> Truncate & Reduce <><><><>" << endl;
    trunc_reduce();
    cout << "<><><><> (Unsigned) Truncate <><><><>" << endl;
    trunc(false);
    cout << "<><><><> (Signed) Truncate <><><><>" << endl;

    trunc(true);

    cout << "<><><><> (Unsigned) Division by power of 2 <><><><>" << endl;
    div_pow2(false);

    cout << "<><><><> (Signed) Division by power of 2 <><><><>" << endl;
    div_pow2(true);
}
