#include "LicenseManager.h"
#include <juce_core/juce_core.h>

LicenseManager::LicenseManager()
{
    // Generate a machine ID from hardware identifiers — not reusable across devices
    auto sysInfo = juce::SystemStats::getUniqueDeviceID();
    auto machineName = juce::SystemStats::getComputerName();
    machineId = juce::String (sysInfo.hash() ^ machineName.hash());

    load();
}

juce::File LicenseManager::getDataFile() const
{
    // Store in a hidden directory in the user's home — survives plugin reinstalls
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("JuicePipe")
                       .getChildFile ("MixMind");

    if (!dir.exists())
        dir.createDirectory();

    // Tie to this specific machine so it can't be copied
    return dir.getChildFile ("license_" + machineId.substring (0, 8) + ".json");
}

void LicenseManager::load()
{
    auto file = getDataFile();
    if (!file.existsAsFile())
        return;

    auto json = file.loadFileAsString();
    if (json.isEmpty())
        return;

    auto obj = juce::JSON::parse (json);
    if (auto* root = obj.getDynamicObject())
    {
        freePromptsUsed = (int) root->getProperty ("freePromptsUsed");
        licenseKey      = root->getProperty ("licenseKey").toString();
        welcomeShown    = root->getProperty ("welcomeShown");
    }
}

void LicenseManager::save()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("freePromptsUsed", freePromptsUsed);
    obj->setProperty ("licenseKey",      licenseKey);
    obj->setProperty ("welcomeShown",    welcomeShown);

    auto file = getDataFile();
    file.replaceWithText (juce::JSON::toString (juce::var (obj)));
}

bool LicenseManager::canPrompt() const
{
    if (isLicensed())  return true;
    if (freePromptsUsed < kFreePromptLimit) return true;
    return false;
}

void LicenseManager::recordPrompt()
{
    if (!isLicensed())
    {
        freePromptsUsed++;
        save();
    }
}

bool LicenseManager::isLicensed() const
{
    return licenseKey.isNotEmpty() && licenseKey.startsWith ("MM-");
}

void LicenseManager::setLicenseKey (const juce::String& key)
{
    licenseKey = key.trim();
    save();
}

juce::String LicenseManager::getLicenseKey() const
{
    return licenseKey;
}

int LicenseManager::getFreePromptsRemaining() const
{
    return juce::jmax (0, kFreePromptLimit - freePromptsUsed);
}

int LicenseManager::getTotalPromptsUsed() const
{
    return freePromptsUsed;
}

bool LicenseManager::hasShownWelcome() const
{
    return welcomeShown;
}

void LicenseManager::markWelcomeShown()
{
    welcomeShown = true;
    save();
}

juce::String LicenseManager::getStatus() const
{
    if (isLicensed())
        return "Licensed ✓";

    auto remaining = getFreePromptsRemaining();
    if (remaining > 0)
        return juce::String (remaining) + " free prompts left";

    return "License required";
}
