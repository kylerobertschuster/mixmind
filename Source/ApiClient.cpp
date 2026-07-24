#include "ApiClient.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Background job — runs the HTTP request off the message thread
// ─────────────────────────────────────────────────────────────────────────────
class ApiRequestJob : public juce::ThreadPoolJob
{
public:
    ApiRequestJob (const juce::URL&     reqUrl,
                   const juce::String&  reqHeaders,
                   const juce::String&  body,
                   ApiClient::Callback  cb)
        : ThreadPoolJob ("MixMindApiRequest"),
          url (reqUrl), headers (reqHeaders), requestBody (body), callback (std::move (cb))
    {}

    JobStatus runJob() override
    {
        ApiClient::Result result;

        try
        {
            int statusCode = 0;
            juce::StringPairArray responseHeaders;

            auto stream = url.withPOSTData (requestBody)
                             .createInputStream (
                                 juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                     .withExtraHeaders (headers)
                                     .withConnectionTimeoutMs (60000)
                                     .withStatusCode (&statusCode)
                                     .withResponseHeaders (&responseHeaders)
                             );

            if (stream == nullptr)
            {
                result.errorMessage = "Network error: could not connect to " + url.toString (false);
            }
            else
            {
                auto responseText = stream->readEntireStreamAsString();

                if (statusCode == 200)
                    result = ApiClient::parseResponse (responseText);
                else
                    result.errorMessage = "API returned status " + juce::String (statusCode)
                                        + ": " + responseText.substring (0, 300);
            }
        }
        catch (const std::exception& e)
        {
            result.errorMessage = juce::String ("Exception: ") + e.what();
        }

        auto cb = callback;
        juce::MessageManager::callAsync ([cb, result]() { cb (result); });

        return JobStatus::jobHasFinished;
    }

private:
    juce::URL            url;
    juce::String         headers;
    juce::String         requestBody;
    ApiClient::Callback  callback;
};

// ─────────────────────────────────────────────────────────────────────────────
//  ApiClient::send
// ─────────────────────────────────────────────────────────────────────────────
void ApiClient::send (const juce::String&             systemPrompt,
                      const std::vector<ChatMessage>& history,
                      Callback                        callback)
{
    juce::URL url (serverUrl + "/api/chat");

    // License key sent as a header — users never need to touch the Anthropic key
    juce::String headers = "content-type: application/json\r\n"
                           "x-license-key: " + licenseKey;

    // Inject real-time session data into the system prompt
    juce::String fullSystem = systemPrompt;
    if (sessionContext.isNotEmpty())
        fullSystem += "\n\n[CURRENT MIX STATE]\n" + sessionContext;

    auto body = buildRequestBody (fullSystem, history, maxTokens);
    threadPool.addJob (new ApiRequestJob (url, headers, body, std::move (callback)), true);
}

void ApiClient::cancelPending()
{
    threadPool.removeAllJobs (true, 2000);
}

// ─────────────────────────────────────────────────────────────────────────────
//  JSON builder  (model is now chosen server-side based on license tier)
// ─────────────────────────────────────────────────────────────────────────────
static juce::String jsonEscape (const juce::String& s)
{
    return s.replace ("\\", "\\\\")
             .replace ("\"", "\\\"")
             .replace ("\n", "\\n")
             .replace ("\r", "\\r")
             .replace ("\t", "\\t");
}

juce::String ApiClient::buildRequestBody (const juce::String&             systemPrompt,
                                          const std::vector<ChatMessage>& history,
                                          int                             maxTokens)
{
    juce::String json;

    json << "{"
         << "\"max_tokens\":" << maxTokens << ","
         << "\"messages\":[";

    // System prompt first
    json << "{\"role\":\"system\",\"content\":\"" << jsonEscape (systemPrompt) << "\"}";

    for (const auto& msg : history)
    {
        juce::String role = (msg.role == ChatMessage::Role::User) ? "user" : "assistant";
        json << ",{\"role\":\"" << role << "\",\"content\":\"" << jsonEscape (msg.content) << "\"}";
    }

    json << "]}";
    return json;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Response parser — proxy returns { content, model, tier }
// ─────────────────────────────────────────────────────────────────────────────
ApiClient::Result ApiClient::parseResponse (const juce::String& jsonText)
{
    Result result;

    // Extract "content":"..."
    int contentPos = jsonText.indexOf ("\"content\":\"");
    if (contentPos >= 0)
    {
        int start = contentPos + 11;
        juce::String extracted;
        bool escaped = false;

        for (int i = start; i < jsonText.length(); ++i)
        {
            auto c = jsonText[i];
            if (escaped)
            {
                if      (c == 'n') extracted += '\n';
                else if (c == 't') extracted += '\t';
                else if (c == 'r') extracted += '\r';
                else               extracted += c;
                escaped = false;
            }
            else if (c == '\\') { escaped = true; }
            else if (c == '"')  { break; }
            else                { extracted += c; }
        }

        result.success = true;
        result.text    = extracted;

        // Extract "tier":"..." so the UI can optionally show which model responded
        int tierPos = jsonText.indexOf ("\"tier\":\"");
        if (tierPos >= 0)
        {
            int ts = tierPos + 8;
            int te = jsonText.indexOf (ts, "\"");
            result.tier = jsonText.substring (ts, te < 0 ? jsonText.length() : te);
        }
    }
    else
    {
        // Check for error field
        int errPos = jsonText.indexOf ("\"error\":\"");
        if (errPos >= 0)
        {
            int start = errPos + 9;
            int end   = jsonText.indexOf (start, "\"");
            result.errorMessage = jsonText.substring (start, end < 0 ? jsonText.length() : end);
        }
        else
        {
            result.errorMessage = "Could not parse proxy response.";
        }
    }

    return result;
}
