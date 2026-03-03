#include "../../data_win.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image_write.h"

#include "../../utils.h"

// ===[ COMMAND LINE ARGUMENTS ]===
typedef struct {
    const char* dataWinPath;
    const char* screenshotPattern;
    int* screenshotFrames;
    int screenshotFrameCount;
    int screenshotFrameCapacity;
    bool headless;
} CommandLineArgs;

static int CommandLineArgs_compareInts(const void* a, const void* b) {
    return *(const int*) a - *(const int*) b;
}

static void CommandLineArgs_parse(CommandLineArgs* args, int argc, char* argv[]) {
    memset(args, 0, sizeof(CommandLineArgs));

    static struct option longOptions[] = {
        {"screenshot",          required_argument, nullptr, 's'},
        {"screenshot-at-frame", required_argument, nullptr, 'f'},
        {"headless",            no_argument,       nullptr, 'h'},
        {nullptr,               0,                 nullptr,  0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "", longOptions, nullptr)) != -1) {
        switch (opt) {
            case 's':
                args->screenshotPattern = optarg;
                break;
            case 'f': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || frame < 0) {
                    fprintf(stderr, "Error: Invalid frame number '%s'\n", optarg);
                    exit(1);
                }
                // Grow the dynamic array if needed
                // We do it like this because the "f" case (which is used for the frame argument) may be called multiple times
                if (args->screenshotFrameCount >= args->screenshotFrameCapacity) {
                    int newCapacity = (args->screenshotFrameCapacity == 0) ? 8 : args->screenshotFrameCapacity * 2;
                    int* newArray = realloc(args->screenshotFrames, newCapacity * sizeof(int));
                    if (newArray == nullptr) {
                        fprintf(stderr, "Error: Failed to allocate memory for screenshot frames\n");
                        exit(1);
                    }
                    args->screenshotFrames = newArray;
                    args->screenshotFrameCapacity = newCapacity;
                }
                args->screenshotFrames[args->screenshotFrameCount++] = (int) frame;
                break;
            }
            case 'h':
                args->headless = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [--headless] [--screenshot=PATTERN] [--screenshot-at-frame=N ...] <path to data.win or game.unx>\n", argv[0]);
                exit(1);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Usage: %s [--headless] [--screenshot=PATTERN] [--screenshot-at-frame=N ...] <path to data.win or game.unx>\n", argv[0]);
        exit(1);
    }

    args->dataWinPath = argv[optind];

    if (args->screenshotFrameCount > 0 && args->screenshotPattern == nullptr) {
        fprintf(stderr, "Error: --screenshot-at-frame requires --screenshot to be set\n");
        exit(1);
    }

    // Sort frames for efficient lookup in the main loop
    if (args->screenshotFrameCount > 0) {
        qsort(args->screenshotFrames, args->screenshotFrameCount, sizeof(int), CommandLineArgs_compareInts);
    }
}

static void CommandLineArgs_free(CommandLineArgs* args) {
    free(args->screenshotFrames);
}

// ===[ SCREENSHOT ]===
static void Screenshot_capture(const char* filenamePattern, int frameNumber, int width, int height) {
    char filename[512];
    snprintf(filename, sizeof(filename), filenamePattern, frameNumber);

    int stride = width * 4;
    unsigned char* pixels = malloc(stride * height);
    if (pixels == nullptr) {
        fprintf(stderr, "Error: Failed to allocate memory for screenshot (%dx%d)\n", width, height);
        return;
    }

    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // OpenGL reads bottom-to-top, but PNG is top-to-bottom.
    // Use stb's negative stride trick: point to the last row and use a negative stride to flip vertically.
    unsigned char* lastRow = pixels + (height - 1) * stride;
    stbi_write_png(filename, width, height, 4, lastRow, -stride);

    free(pixels);
    printf("Screenshot saved: %s\n", filename);
}

// ===[ MAIN ]===
int main(int argc, char* argv[]) {
    CommandLineArgs args;
    CommandLineArgs_parse(&args, argc, argv);

    printf("Loading %s...\n", args.dataWinPath);

    DataWin* dataWin = DataWin_parse(args.dataWinPath);

    Gen8* gen8 = &dataWin->gen8;
    printf("Loaded \"%s\" (%d) successfully!\n", gen8->name, gen8->gameID);

    // Build window title
    char windowTitle[256];
    snprintf(windowTitle, sizeof(windowTitle), "Butterscotch - %s", gen8->displayName);

    // Init GLFW
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        DataWin_free(dataWin);
        CommandLineArgs_free(&args);
        return 1;
    }

    if (args.headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    GLFWwindow* window = glfwCreateWindow((int) gen8->defaultWindowWidth, (int) gen8->defaultWindowHeight, windowTitle, nullptr, nullptr);
    if (window == nullptr) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        DataWin_free(dataWin);
        CommandLineArgs_free(&args);
        return 1;
    }

    glfwMakeContextCurrent(window);

    // Load OpenGL function pointers via GLAD
    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        DataWin_free(dataWin);
        CommandLineArgs_free(&args);
        return 1;
    }

    // Main loop
    int frameCount = 0;
    int takenScreenshots = 0;
    while (!glfwWindowShouldClose(window)) {
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Capture screenshot if this frame matches the next requested frame
        // This is a bit slow (because we are checking each frame and looping through all the screenshot checks), but it doesn't matter
        int* frameToBeTaken;
        forEach(frameToBeTaken, args.screenshotFrames, args.screenshotFrameCount) {
            if (*frameToBeTaken == frameCount) {
                Screenshot_capture(args.screenshotPattern, frameCount, (int) gen8->defaultWindowWidth, (int) gen8->defaultWindowHeight);

                takenScreenshots++;

                if (takenScreenshots == args.screenshotFrameCount) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
            }
        }

        frameCount++;

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    glfwDestroyWindow(window);
    glfwTerminate();
    DataWin_free(dataWin);
    CommandLineArgs_free(&args);
    return 0;
}
