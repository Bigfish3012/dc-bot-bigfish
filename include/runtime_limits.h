#ifndef RUNTIME_LIMITS_H
#define RUNTIME_LIMITS_H

#include <cstddef>

// Conservative defaults for a Raspberry Pi; byte limits apply before parsing.
namespace limits {
inline constexpr std::size_t workers = 2;
inline constexpr std::size_t outstanding_jobs = 32;
inline constexpr std::size_t jobs_per_session = 4;
inline constexpr std::size_t attachment_count = 5;
inline constexpr std::size_t attachment_bytes = 2 * 1024 * 1024;
inline constexpr std::size_t attachment_total_bytes = 16 * 1024 * 1024;
inline constexpr std::size_t input_bytes = 64 * 1024;
inline constexpr std::size_t api_response_bytes = 1024 * 1024;
inline constexpr std::size_t moderation_response_bytes = 128 * 1024;
inline constexpr std::size_t history_bytes = 256 * 1024;
inline constexpr std::size_t history_sessions = 128;
inline constexpr std::size_t document_total_bytes = 4 * 1024 * 1024;
inline constexpr std::size_t image_input_bytes = 8 * 1024 * 1024;
inline constexpr std::size_t image_response_bytes = 16 * 1024 * 1024;
inline constexpr std::size_t generated_image_bytes = 8 * 1024 * 1024;
inline constexpr std::size_t image_prompt_bytes = 32000;
inline constexpr long image_seconds = 240;
inline constexpr long connect_seconds = 10;
inline constexpr long download_seconds = 30;
inline constexpr long moderation_seconds = 20;
inline constexpr long chat_seconds = 180;
inline constexpr int pdf_seconds = 15;
inline constexpr std::size_t pdf_memory_bytes = 256 * 1024 * 1024;
inline constexpr int pdf_cpu_seconds = 10;
}
#endif
