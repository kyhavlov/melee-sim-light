#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "puffernet.h"

typedef struct {
    Weights* weights;
    PufferNet* net;
    float actions[2];
    int obs_size;
    int stochastic;
    uint32_t rng_state;
} PufferInfer;

static uint32_t puffer_next_u32(PufferInfer* infer) {
    uint32_t x = infer->rng_state;
    if (x == 0) x = 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    infer->rng_state = x;
    return x;
}

static float puffer_next_f32(PufferInfer* infer) {
    return (puffer_next_u32(infer) >> 8) * (1.0f / 16777216.0f);
}

static void puffer_sample_multidiscrete(PufferInfer* infer, float* input, float* output) {
    int logit_sizes[2] = {416, 85};
    int in_adr = 0;
    for (int a = 0; a < 2; a++) {
        int num_action_types = logit_sizes[a];
        float max_logit = input[in_adr];
        for (int i = 1; i < num_action_types; i++) {
            if (input[in_adr + i] > max_logit) {
                max_logit = input[in_adr + i];
            }
        }
        float sum = 0.0f;
        for (int i = 0; i < num_action_types; i++) {
            sum += expf(input[in_adr + i] - max_logit);
        }
        float cutoff = puffer_next_f32(infer) * sum;
        float accum = 0.0f;
        int choice = num_action_types - 1;
        for (int i = 0; i < num_action_types; i++) {
            accum += expf(input[in_adr + i] - max_logit);
            if (cutoff <= accum) {
                choice = i;
                break;
            }
        }
        output[a] = (float)choice;
        in_adr += num_action_types;
    }
}

void* puffer_infer_create(const char* checkpoint_path, int obs_size, int hidden_size, int num_layers,
        int stochastic, uint32_t seed) {
    int act_sizes[2] = {416, 85};
    Weights* weights = load_weights(checkpoint_path);
    if (weights == NULL) {
        return NULL;
    }

    PufferInfer* infer = (PufferInfer*)calloc(1, sizeof(PufferInfer));
    if (infer == NULL) {
        free(weights);
        return NULL;
    }

    infer->weights = weights;
    infer->obs_size = obs_size;
    infer->stochastic = stochastic;
    infer->rng_state = seed ? seed : 0x9e3779b9u;
    infer->net = make_puffernet(weights, 1, obs_size, hidden_size, num_layers, act_sizes, 2);
    if (infer->net == NULL) {
        free(weights);
        free(infer);
        return NULL;
    }
    return infer;
}

void puffer_infer_reset(void* handle) {
    if (handle == NULL) return;
    PufferInfer* infer = (PufferInfer*)handle;
    memset(infer->net->mingru->state, 0,
        (size_t)infer->net->mingru->num_layers * infer->net->mingru->batch_size *
        infer->net->mingru->hidden_size * sizeof(float));
}

int puffer_infer_step(void* handle, const float* observation, int* button_label, int* main_label) {
    if (handle == NULL || observation == NULL || button_label == NULL || main_label == NULL) {
        return -1;
    }

    PufferInfer* infer = (PufferInfer*)handle;
    memcpy(infer->net->obs, observation, (size_t)infer->obs_size * sizeof(float));
    linear(infer->net->encoder, infer->net->obs);
    mingru(infer->net->mingru, infer->net->encoder->output);
    linear(infer->net->decoder, infer->net->mingru->output);
    if (infer->stochastic) {
        puffer_sample_multidiscrete(infer, infer->net->decoder->output, infer->actions);
    } else {
        argmax_multidiscrete(infer->net->multidiscrete, infer->net->decoder->output, infer->actions);
    }
    *button_label = (int)infer->actions[0];
    *main_label = (int)infer->actions[1];
    return 0;
}

void puffer_infer_destroy(void* handle) {
    if (handle == NULL) return;
    PufferInfer* infer = (PufferInfer*)handle;
    if (infer->net != NULL) {
        free_puffernet(infer->net);
    }
    free(infer->weights);
    free(infer);
}
