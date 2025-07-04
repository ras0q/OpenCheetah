/*
Authors: Deevashwer Rathee
Copyright:
Copyright (c) 2020 Microsoft Research
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

#include "LinearHE/utils-HE.h"

#include "seal/util/polyarithsmallmod.h"

using namespace std;
using namespace sci;
using namespace seal;
using namespace seal::util;

void generate_new_keys(
    int party,
    NetIO *io,
    int slot_count,
    SEALContext *&context_,
    Encryptor *&encryptor_,
    Decryptor *&decryptor_,
    Evaluator *&evaluator_,
    BatchEncoder *&encoder_,
    GaloisKeys *&gal_keys_,
    Ciphertext *&zero_,
    bool verbose
) {
    EncryptionParameters parms(scheme_type::bfv);
    parms.set_poly_modulus_degree(slot_count);
    parms.set_coeff_modulus(CoeffModulus::Create(slot_count, {60, 60, 60, 49}));
    parms.set_plain_modulus(prime_mod);
    context_ = new SEALContext(parms, true, sec_level_type::none);
    encoder_ = new BatchEncoder(*context_);
    evaluator_ = new Evaluator(*context_);

    if (party == BOB) {
        KeyGenerator keygen(*context_);
        SecretKey sec_key = keygen.secret_key();
        Serializable<PublicKey> pub_key = keygen.create_public_key();
        Serializable<GaloisKeys> gal_keys = keygen.create_galois_keys();

        stringstream os;
        pub_key.save(os);
        uint64_t pk_size = os.tellp();
        gal_keys.save(os);
        uint64_t gk_size = (uint64_t)os.tellp() - pk_size;

        string keys_ser = os.str();
        io->send_data(&pk_size, sizeof(uint64_t));
        io->send_data(&gk_size, sizeof(uint64_t));
        io->send_data(keys_ser.c_str(), pk_size + gk_size);

        PublicKey pk;
        pk.load(*context_, os);
        encryptor_ = new Encryptor(*context_, pk);
        decryptor_ = new Decryptor(*context_, sec_key);
        gal_keys_ = new GaloisKeys();
        gal_keys_->load(*context_, os);
#ifdef HE_DEBUG
        stringstream os_sk;
        sec_key.save(os_sk);
        uint64_t sk_size = os_sk.tellp();
        string keys_ser_sk = os_sk.str();
        io->send_data(&sk_size, sizeof(uint64_t));
        io->send_data(keys_ser_sk.c_str(), sk_size);
#endif
    } else  // party == ALICE
    {
        uint64_t pk_size;
        uint64_t gk_size;
        io->recv_data(&pk_size, sizeof(uint64_t));
        io->recv_data(&gk_size, sizeof(uint64_t));
        char *key_share = new char[pk_size + gk_size];
        io->recv_data(key_share, pk_size + gk_size);
        stringstream is;
        PublicKey pub_key;
        is.write(key_share, pk_size);
        pub_key.load(*context_, is);
        gal_keys_ = new GaloisKeys();
        is.write(key_share + pk_size, gk_size);
        gal_keys_->load(*context_, is);
        delete[] key_share;

        encryptor_ = new Encryptor(*context_, pub_key);
        vector<uint64_t> pod_matrix(slot_count, 0ULL);
        Plaintext tmp;
        encoder_->encode(pod_matrix, tmp);
        zero_ = new Ciphertext;
        encryptor_->encrypt(tmp, *zero_);

#ifdef HE_DEBUG
        uint64_t sk_size;
        io->recv_data(&sk_size, sizeof(uint64_t));
        char *key_share_sk = new char[sk_size];
        io->recv_data(key_share_sk, sk_size);
        stringstream is_sk;
        SecretKey sec_key;
        is_sk.write(key_share_sk, sk_size);
        sec_key.load(context_, is_sk);
        delete[] key_share_sk;
        decryptor_ = new Decryptor(context_, sec_key);
#endif
    }

    if (verbose)
        cout << "Keys Generated (slot_count: " << slot_count << ")" << endl;
}

void free_keys(
    int party,
    Encryptor *&encryptor_,
    Decryptor *&decryptor_,
    Evaluator *&evaluator_,
    BatchEncoder *&encoder_,
    GaloisKeys *&gal_keys_,
    Ciphertext *&zero_
) {
    delete encoder_;
    delete evaluator_;
    delete encryptor_;
    if (party == BOB) {
        delete decryptor_;
    } else  // party ==ALICE
    {
#ifdef HE_DEBUG
        delete decryptor_;
#endif
        delete gal_keys_;
        delete zero_;
    }
}

void send_encrypted_vector(NetIO *io, const vector<Ciphertext> &ct_vec) {
    assert(ct_vec.size() > 0);
    uint32_t ncts = ct_vec.size();
    io->send_data(&ncts, sizeof(uint32_t));
    for (size_t i = 0; i < ncts; ++i) {
        send_ciphertext(io, ct_vec[i]);
    }
}

void recv_encrypted_vector(
    NetIO *io, const SEALContext &context, vector<Ciphertext> &ct_vec
) {
    uint32_t ncts{0};
    io->recv_data(&ncts, sizeof(uint32_t));
    ct_vec.resize(ncts);
    for (size_t i = 0; i < ncts; ++i) {
        recv_ciphertext(io, context, ct_vec[i]);
    }
}

void send_ciphertext(NetIO *io, const Ciphertext &ct) {
    stringstream os;
    uint64_t ct_size;
    ct.save(os);
    ct_size = os.tellp();
    string ct_ser = os.str();
    io->send_data(&ct_size, sizeof(uint64_t));
    io->send_data(ct_ser.c_str(), ct_ser.size());
}

void recv_ciphertext(NetIO *io, const SEALContext &context, Ciphertext &ct) {
    stringstream is;
    uint64_t ct_size;
    io->recv_data(&ct_size, sizeof(uint64_t));
    char *c_enc_result = new char[ct_size];
    io->recv_data(c_enc_result, ct_size);
    is.write(c_enc_result, ct_size);
    ct.unsafe_load(context, is);
    delete[] c_enc_result;
}

void set_poly_coeffs_uniform(
    uint64_t *poly,
    uint32_t bitlen,
    shared_ptr<UniformRandomGenerator> random,
    shared_ptr<const SEALContext::ContextData> &context_data
) {
    assert(bitlen < 128 && bitlen > 0);
    auto &parms = context_data->parms();
    auto &coeff_modulus = parms.coeff_modulus();
    size_t coeff_count = parms.poly_modulus_degree();
    size_t coeff_mod_count = coeff_modulus.size();
    uint64_t bitlen_mask = (1ULL << (bitlen % 64)) - 1;

    RandomToStandardAdapter engine(random);
    for (size_t i = 0; i < coeff_count; i++) {
        if (bitlen < 64) {
            uint64_t noise = (uint64_t(engine()) << 32) | engine();
            noise &= bitlen_mask;
            for (size_t j = 0; j < coeff_mod_count; j++) {
                poly[i + (j * coeff_count)] =
                    barrett_reduce_64(noise, coeff_modulus[j]);
            }
        } else {
            uint64_t noise[2];  // LSB || MSB
            for (int j = 0; j < 2; j++) {
                noise[0] = (uint64_t(engine()) << 32) | engine();
                noise[1] = (uint64_t(engine()) << 32) | engine();
            }
            noise[1] &= bitlen_mask;
            for (size_t j = 0; j < coeff_mod_count; j++) {
                poly[i + (j * coeff_count)] =
                    barrett_reduce_128(noise, coeff_modulus[j]);
            }
        }
    }
}

void flood_ciphertext(
    Ciphertext &ct,
    shared_ptr<const SEALContext::ContextData> &context_data,
    uint32_t noise_len,
    MemoryPoolHandle pool
) {
    auto &parms = context_data->parms();
    auto &coeff_modulus = parms.coeff_modulus();
    size_t coeff_count = parms.poly_modulus_degree();
    size_t coeff_mod_count = coeff_modulus.size();

    auto noise(allocate_poly(coeff_count, coeff_mod_count, pool));
    shared_ptr<UniformRandomGenerator> random(parms.random_generator()->create()
    );

    set_poly_coeffs_uniform(noise.get(), noise_len, random, context_data);
    for (size_t i = 0; i < coeff_mod_count; i++) {
        add_poly_coeffmod(
            noise.get() + (i * coeff_count), ct.data() + (i * coeff_count),
            coeff_count, coeff_modulus[i], ct.data() + (i * coeff_count)
        );
    }

    set_poly_coeffs_uniform(noise.get(), noise_len, random, context_data);
    for (size_t i = 0; i < coeff_mod_count; i++) {
        add_poly_coeffmod(
            noise.get() + (i * coeff_count), ct.data(1) + (i * coeff_count),
            coeff_count, coeff_modulus[i], ct.data(1) + (i * coeff_count)
        );
    }
}
