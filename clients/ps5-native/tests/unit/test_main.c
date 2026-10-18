#include <stdio.h>

#include "test.h"

int g_failures = 0;
int g_checks = 0;

void run_nav_tests(void);
void run_input_tests(void);
void run_audio_tests(void);
void run_redact_tests(void);
void run_context_tests(void);
void run_image_tests(void);
void run_text_tests(void);
void run_ui_tests(void);
void run_script_tests(void);
void run_account_tests(void);
void run_social_tests(void);
void run_picture_tests(void);
void run_artwork_tests(void);
void run_chat_tests(void);
void run_party_tests(void);
void run_voice_tests(void);

int main(void) {
    run_nav_tests();
    run_input_tests();
    run_audio_tests();
    run_redact_tests();
    run_context_tests();
    run_image_tests();
    run_text_tests();
    run_ui_tests();
    run_script_tests();
    run_account_tests();
    run_social_tests();
    run_picture_tests();
    run_artwork_tests();
    run_chat_tests();
    run_party_tests();
    run_voice_tests();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
