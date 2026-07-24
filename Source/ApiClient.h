#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Message  — a single chat turn
// ─────────────────────────────────────────────────────────────────────────────
struct ChatMessage
{
    enum class Role { User, Assistant };
    Role           role;
    juce::String   content;
};

// ─────────────────────────────────────────────────────────────────────────────
//  ApiClient
//  Sends messages to the MixMind proxy server on a background thread.
//  Users authenticate with a MixMind license key — no Anthropic key needed.
//  Standard tier routes to Groq (fast, cheap). Premium tier routes to Claude.
// ─────────────────────────────────────────────────────────────────────────────
class ApiClient
{
public:
    struct Result
    {
        bool          success { false };
        juce::String  text;
        juce::String  errorMessage;
        juce::String  tier;   // "standard" or "premium" — echoed back by proxy
    };

    using Callback = std::function<void (Result)>;

    ApiClient() = default;
    ~ApiClient() { cancelPending(); }

    // ── Configuration ──────────────────────────────────────────────────────
    void setMaxTokens      (int tokens)                 { maxTokens = tokens; }
    void setSessionContext (const juce::String& json)   { sessionContext = json; }
    void setLicenseKey     (const juce::String& key)    { licenseKey = key; }
    void setServerUrl      (const juce::String& url)    { serverUrl = url; }

    // ── Send a conversation ────────────────────────────────────────────────
    void send (const juce::String&              systemPrompt,
               const std::vector<ChatMessage>&  history,
               Callback                         callback);

    void cancelPending();

    // ── Build a JSON body ──────────────────────────────────────────────────
    static juce::String buildRequestBody (const juce::String&             systemPrompt,
                                          const std::vector<ChatMessage>& history,
                                          int                             maxTokens);

    // ── Parse the response ─────────────────────────────────────────────────
    static Result parseResponse (const juce::String& jsonText);

private:
    juce::String serverUrl     { "https://your-mixmind-server.com" }; // replace before shipping
    juce::String licenseKey;
    juce::String sessionContext;
    int          maxTokens     { 1000 };

    juce::ThreadPool threadPool { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApiClient)
};
