#pragma once

#include "../services/AIService.h"
#include "../services/LanguageManager.h"
#include "DjButton.h"
#include "LucideIcons.h"
#include <vector>

class AIMatchView : public juce::Component,
                    public LanguageManager::Listener
{
public:
    AIMatchView();
    ~AIMatchView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void languageChanged() override;
    void refreshLanguage();

    // Updates currently active track context from Deck 1 or Deck 2 or YouTube
    void updateActiveTrack(const CurrentTrackContext& ctx);

    // Provide local library files for hybrid resolution
    void setLocalLibraryFiles(const std::vector<juce::File>& files);

    // Callbacks to MainComponent / LibraryComponent
    std::function<void(const juce::File& file, int targetDeck)> onLoadLocalTrack;
    std::function<void(const juce::String& query, int targetDeck)> onSearchYouTube;
    std::function<void()> onOpenSettings;

    void refreshRecommendations();

private:
    CurrentTrackContext currentContext;
    std::vector<juce::File> localFiles;
    std::vector<AIRecommendation> recommendations;
    bool resultsUncertain { false }; // known:false -> la IA no reconoce la pista
    juce::String lastPrefetchedVideoId;
    int consentedProviderId { -1 };

    bool isLoading { false };
    juce::String statusMessage;
    juce::String errorMessage;

    // Header Controls
    DjButton refreshButton { "Generar Recomendaciones" };
    DjButton settingsShortcutButton { "Configurar IA" };

    // Scrollable container for recommendation cards
    class CardListContainer;
    std::unique_ptr<CardListContainer> listContainer;
    juce::Viewport viewport;

    void updateCardList();
    void startRecommendationsRequest();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIMatchView)
};
