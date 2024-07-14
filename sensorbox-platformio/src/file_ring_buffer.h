#pragma once
#include <LittleFS.h>
#include <Preferences.h>
#include <my_buffers.h>
#include <assert.h>

#define MAX_FILENAME_SIZE 32

const char *TAG_FRB = "frb";

class FileRingBuffer
{
private:
    const char *nameSpace;
    int maxNumFiles = -1;
    int totalEntries = -1;
    bool began = false;
    File currentFile;
    Preferences frb_prefs;
    SemaphoreHandle_t mutex;

    void saveMetaToPrefs()
    {
        frb_prefs.begin(nameSpace, false);
        frb_prefs.putInt("head", headFileIndex);
        frb_prefs.putInt("tail", currentFileIndex);
        frb_prefs.putInt("total", totalEntries);
        frb_prefs.end();
    }

    void loadMetaFromPrefs()
    {
        frb_prefs.begin(nameSpace, true);
        currentFileIndex = frb_prefs.getInt("tail", 0);
        headFileIndex = frb_prefs.getInt("head", 0);
        totalEntries = frb_prefs.getInt("total", 0);
        frb_prefs.end();
    }

public:
    int headFileIndex;
    int currentFileIndex;
    const int blockSize = 1024 * 4;

    // if maxNumFiles is -1, then the maxNumFiles will be set to the maximum number of files that can fit into the flash
    FileRingBuffer(const char *nameSpace = "ring_buffer", int maxNumFiles = -1)
    {
        this->nameSpace = nameSpace;
        this->maxNumFiles = maxNumFiles;
    }

    void beginPrefs()
    {
        loadMetaFromPrefs();
    }

    void begin()
    {
        if (began)
            return;

        if (!LittleFS.begin(true))
        {
            ESP_LOGE(TAG_FRB, "An Error has occurred while mounting LittleFS");
            return;
        }

        if (maxNumFiles == -1)
            maxNumFiles = (LittleFS.totalBytes() - 100 * 1024) / blockSize;
        mutex = xSemaphoreCreateMutex();

        if (totalEntries == -1)
            beginPrefs();

        began = true;
    }

    void pushRtcBuffer(ReadingsBuffer *readingsBuffer)
    {
        char filePath[MAX_FILENAME_SIZE];

        xSemaphoreTake(mutex, portMAX_DELAY);

        size_t i = 0;
        size_t totalEntriesToWrite = readingsBufferCount(readingsBuffer);
        while (i < totalEntriesToWrite)
        {
            snprintf(filePath, MAX_FILENAME_SIZE, "/%s/%d.bin", nameSpace, currentFileIndex);
            // Open the current file
            currentFile = LittleFS.open(filePath, "a");

            size_t lastFileSize = currentFile.size();

            // Calculate the number of entries that can fit into the current file
            size_t numEntries = (blockSize - lastFileSize) / sizeof(Readings);
            if (numEntries > totalEntriesToWrite - i)
            {
                numEntries = totalEntriesToWrite - i;
            }

            // If the file doesn't exist or there isn't enough space for the new entries, create a new file
            if (!currentFile || numEntries == 0)
            {
                currentFile.close();

                // Increment the file index, wrapping around to 0 if it exceeds maxNumFiles
                currentFileIndex = (currentFileIndex + 1) % maxNumFiles;
                if (currentFileIndex == headFileIndex)
                {
                    // If we've caught up to the head, move the head forward
                    headFileIndex = (headFileIndex + 1) % maxNumFiles;
                    totalEntries -= lastFileSize / sizeof(Readings);
                }

                snprintf(filePath, MAX_FILENAME_SIZE, "/%s/%d.bin", nameSpace, currentFileIndex);
                currentFile = LittleFS.open(filePath, "w", true);

                Serial.printf("Opened file %s\n", filePath);

                // Since we created a new file, we can write blockSize / sizeof(Readings) entries to it
                numEntries = blockSize / sizeof(Readings);

                if (numEntries > totalEntriesToWrite - i)
                {
                    numEntries = totalEntriesToWrite - i;
                }
            }

            // Write the entries to the current file
            if (currentFile)
            {
                for (size_t j = 0; j < numEntries; j++)
                {
                    Readings currentEntry = readingsBuffer->buffer[(readingsBuffer->tail + i + j) % READINGS_BUFFER_SIZE];
                    currentFile.write((uint8_t *)&currentEntry, sizeof(Readings));
                }
                totalEntries += numEntries;
                currentFile.close();
            }
            else
            {
                ESP_LOGE(TAG_FRB, "Failed to open file for writing: %s\n", filePath);
            }

            i += numEntries;
        }

        // Close the file and save the metadata after all entries have been written
        currentFile.close();
        saveMetaToPrefs();

        xSemaphoreGive(mutex);
    }

    size_t popFile(Readings *entries)
    {
        char filePath[MAX_FILENAME_SIZE];
        int numEntries = 0;
        File file;

        xSemaphoreTake(mutex, portMAX_DELAY);

        if (totalEntries <= 0)
        {
            ESP_LOGW(TAG_FRB, "Buffer is empty");
            goto finish;
        }
        snprintf(filePath, MAX_FILENAME_SIZE, "/%s/%d.bin", nameSpace, headFileIndex);

        Serial.printf("Popping file %s\n", filePath);

        // Open the head file
        file = LittleFS.open(filePath, "r");

        if (!file)
        {
            ESP_LOGE(TAG_FRB, "Failed to open file for reading (popFile): %s\n", filePath);
            goto finish;
        }

        // Calculate the number of entries in the file
        numEntries = file.size() / sizeof(Readings);

        // Read all entries from the file
        for (int i = 0; i < numEntries; i++)
        {
            if (file.readBytes((char *)&entries[i], sizeof(Readings)) != sizeof(Readings))
            {
                ESP_LOGE(TAG_FRB, "Failed to read entry");
            }
        }

        file.close();

        // Delete the file and move the head forward
        LittleFS.remove(filePath);

        totalEntries -= numEntries;

        if (totalEntries > 0)
            headFileIndex = (headFileIndex + 1) % maxNumFiles;

    finish:

        if (totalEntries < 0)
            totalEntries = 0;

        // Save the metadata
        saveMetaToPrefs();

        xSemaphoreGive(mutex);

        return numEntries;
    }

    int size()
    {
        return totalEntries;
    }

    void iterate(void (*callback)(Readings *))
    {
        char filePath[MAX_FILENAME_SIZE];

        xSemaphoreTake(mutex, portMAX_DELAY);

        // Start iterating from the head file
        int fileIndex = headFileIndex;

        if (totalEntries <= 0)
        {
            goto finish;
        }

        do
        {
            snprintf(filePath, MAX_FILENAME_SIZE, "/%s/%d.bin", nameSpace, fileIndex);

            // Open the file
            File file = LittleFS.open(filePath, "r");

            Serial.printf("Iterating over file %s\n", filePath);

            if (!file)
            {
                ESP_LOGE(TAG_FRB, "Failed to open file for reading (iterate): %s\n", filePath);
                goto finish;
            }

            // Read and process all entries in the file
            while (file.available() >= sizeof(Readings))
            {
                Readings entry;
                if (file.readBytes((char *)&entry, sizeof(entry)) == sizeof(entry))
                {
                    callback(&entry);
                }
                else
                {
                    ESP_LOGE(TAG_FRB, "Failed to read entry");
                }
            }

            file.close();

            // Move to the next file
            fileIndex = (fileIndex + 1) % maxNumFiles;
        } while (fileIndex != (currentFileIndex + 1) % maxNumFiles);

    finish:
        xSemaphoreGive(mutex);
    }

    void clear()
    {
        char filePath[MAX_FILENAME_SIZE];

        xSemaphoreTake(mutex, portMAX_DELAY);

        if (totalEntries <= 0)
        {
            xSemaphoreGive(mutex);
            return;
        }

        snprintf(filePath, MAX_FILENAME_SIZE, "/%s", nameSpace);
        // Delete all files in the dir
        File dir = LittleFS.open(filePath);

        if (!dir)
        {
            ESP_LOGE(TAG_FRB, "Failed to open directory");
            goto finish;
        }

        while (true)
        {
            File entry = dir.openNextFile();
            if (!entry)
            {
                // No more files
                break;
            }
            snprintf(filePath, MAX_FILENAME_SIZE, "/%s/%s", nameSpace, entry.name());

            entry.close();
            LittleFS.remove(filePath);
        }

        // Reset the metadata
        headFileIndex = 0;
        currentFileIndex = 0;
        totalEntries = 0;

        // Save the metadata
        saveMetaToPrefs();

    finish:
        xSemaphoreGive(mutex);
    }

    void listFilesAndMeta()
    {
        char filePath[MAX_FILENAME_SIZE];

        xSemaphoreTake(mutex, portMAX_DELAY);

        snprintf(filePath, MAX_FILENAME_SIZE, "/%s", nameSpace);

        // Open the directory
        File dir = LittleFS.open(filePath);

        if (!dir)
        {
            ESP_LOGE(TAG_FRB, "Failed to open directory");
            goto finish;
        }

        // Iterate over the files in the directory
        while (true)
        {
            File entry = dir.openNextFile();
            if (!entry)
            {
                // No more files
                break;
            }

            // Print the file name, size, and last modified date
            Serial.printf("%s (%d bytes, last modified %lld)\n", entry.name(), entry.size(), entry.getLastWrite());

            entry.close();
        }

        dir.close();

        Serial.printf("headFileIndex: %d, currentFileIndex: %d, totalEntries: %d\n", headFileIndex, currentFileIndex, totalEntries);

    finish:
        xSemaphoreGive(mutex);
    }
};

FileRingBuffer frb;

void testFileRingBuffer()
{
    Readings r = invalidReadings;

    frb.begin();
    frb.clear();

    for (size_t i = 0; i < READINGS_BUFFER_SIZE - 5; i++)
    {
        r.timestampS = i;
        r.freeHeap = ESP.getFreeHeap();

        readingsBufferPush(&readingsBuffer, r);
    }

    frb.pushRtcBuffer(&readingsBuffer);
    frb.pushRtcBuffer(&readingsBuffer);
    frb.pushRtcBuffer(&readingsBuffer);

    assert(frb.size() == 3 * readingsBufferCount(&readingsBuffer));

    Readings entries[frb.blockSize / sizeof(Readings)];
    size_t numEntries = frb.popFile(entries);
    assert(numEntries > 0);
    assert(frb.size() == 3 * readingsBufferCount(&readingsBuffer) - numEntries);

    frb.pushRtcBuffer(&readingsBuffer);
    frb.pushRtcBuffer(&readingsBuffer);
    frb.pushRtcBuffer(&readingsBuffer);
    assert(frb.size() == 6 * readingsBufferCount(&readingsBuffer) - numEntries);

    while (frb.size() > 0)
    {
        numEntries = frb.popFile(entries);
        frb.listFilesAndMeta();
    }
    assert(frb.size() == 0);

    frb.listFilesAndMeta();

    frb.pushRtcBuffer(&readingsBuffer);
    frb.listFilesAndMeta();
    frb.pushRtcBuffer(&readingsBuffer);
    frb.listFilesAndMeta();

    while (frb.size() > 0)
    {
        numEntries = frb.popFile(entries);
        assert(numEntries > 0);
    }
    assert(frb.size() == 0);
    frb.listFilesAndMeta();
}