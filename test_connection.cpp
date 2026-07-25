#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

int main()
{
    juce::URL url("https://getjuicepipe.com/api/chat");
    
    juce::String headers = "content-type: application/json\r\n"
                           "x-license-key: MM-F3DAAA9B42434D47";
    
    juce::String body = R"({"messages":[{"role":"user","content":"Say test"}],"max_tokens":10})";
    
    int statusCode = 0;
    
    std::cout << "Connecting to: " << url.toString(false) << std::endl;
    std::cout << "Headers: " << headers << std::endl;
    std::cout << "Body: " << body << std::endl;
    std::cout << "---" << std::endl;
    
    auto stream = url.withPOSTData(body)
        .createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                .withExtraHeaders(headers)
                .withConnectionTimeoutMs(10000)
                .withStatusCode(&statusCode)
        );
    
    if (stream == nullptr)
    {
        std::cout << "FAILED: stream is null — network error!" << std::endl;
        return 1;
    }
    
    auto response = stream->readEntireStreamAsString();
    std::cout << "Status: " << statusCode << std::endl;
    std::cout << "Response: " << response << std::endl;
    
    return 0;
}
