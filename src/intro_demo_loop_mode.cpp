#include "intro_demo_loop_mode.hpp"

#include "menu_mode.hpp"

#include "common/game_service_provider.hpp"
#include "loader/resource_loader.hpp"
#include "ui/duke_script_runner.hpp"


namespace rigel {

namespace {

struct ScriptExecutionStage {
  ui::DukeScriptRunner* mpScriptRunner;
  data::script::Script mScript;
};


void startStage(ScriptExecutionStage& stage) {
  stage.mpScriptRunner->executeScript(stage.mScript);
}


void updateStage(ScriptExecutionStage& stage, const engine::TimeDelta dt) {
  stage.mpScriptRunner->updateAndRender(dt);
}


bool isStageFinished(const ScriptExecutionStage& stage) {
  return stage.mpScriptRunner->hasFinishedExecution();
}


bool canStageHandleEvents(const ScriptExecutionStage&) {
  return true;
}


void forwardEventToStage(
  const ScriptExecutionStage& stage,
  const SDL_Event& event
) {
  stage.mpScriptRunner->handleEvent(event);
}

}


IntroDemoLoopMode::IntroDemoLoopMode(
  Context context,
  const bool isDuringGameStartup
)
  : mContext(context)
  , mpServiceProvider(context.mpServiceProvider)
  , mFirstRunIncludedStoryAnimation(isDuringGameStartup)
  , mpScriptRunner(context.mpScriptRunner)
  , mScripts(context.mpResources->loadScriptBundle("TEXT.MNI"))
  , mCurrentStage(isDuringGameStartup ? 0 : 1)
{
  mStages.emplace_back(ui::ApogeeLogo(context));
  mStages.emplace_back(ui::IntroMovie(context));
  if (isDuringGameStartup) {
    mStages.emplace_back(ScriptExecutionStage{
      mpScriptRunner,
      mScripts["&Story"]});
  }

  auto creditsScript = mScripts["&Credits"];
  creditsScript.emplace_back(data::script::Delay{700});
  mStages.emplace_back(ScriptExecutionStage{
    mpScriptRunner,
    std::move(creditsScript)});

  // The credits screen is shown twice as long in the registered version. This
  // makes the timing equivalent between the versions, only that the shareware
  // version will switch to the order info screen after half the time has
  // elapsed.
  //
  // Consequently, we always insert two 700 tick delays, but only insert the
  // order info script commands if we're running the shareware version.
  auto orderInfoScript = data::script::Script{};
  if (context.mpServiceProvider->isShareWareVersion()) {
    orderInfoScript = mScripts["Q_ORDER"];
  }
  orderInfoScript.emplace_back(data::script::Delay{700});
  mStages.emplace_back(ScriptExecutionStage{
    mpScriptRunner,
    std::move(orderInfoScript)});

  startStage(mStages[mCurrentStage]);
}


bool IntroDemoLoopMode::handleEvent(const SDL_Event& event) {
  if ((event.type != SDL_KEYDOWN) && (event.type != SDL_CONTROLLERBUTTONDOWN)) {
    return false;
  }

  if (mCurrentStage == 0) {
    // Pressing any key on the Apogee Logo skips forward to the intro movie
    updateStage(mStages[mCurrentStage], 0);
    mpServiceProvider->fadeOutScreen();
    mCurrentStage = 1;

    startStage(mStages[mCurrentStage]);
    updateStage(mStages[mCurrentStage], 0);
    mpServiceProvider->fadeInScreen();
  } else {
    auto& currentStage = mStages[mCurrentStage];

    if (
      (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) ||
      (event.type == SDL_CONTROLLERBUTTONDOWN &&
       event.cbutton.button == SDL_CONTROLLER_BUTTON_B) ||
      !canStageHandleEvents(currentStage)
    ) {
      return true;
    } else {
      forwardEventToStage(currentStage, event);
    }
  }

  return false;
}


std::unique_ptr<GameMode> IntroDemoLoopMode::updateAndRender(
  const engine::TimeDelta dt,
  const std::vector<SDL_Event>& events
) {
  for (const auto& event : events) {
    const auto shouldQuit = handleEvent(event);
    if (shouldQuit) {
      updateStage(mStages[mCurrentStage], 0.0);
      mpServiceProvider->fadeOutScreen();
      return std::make_unique<MenuMode>(mContext);
    }
  }

  updateStage(mStages[mCurrentStage], dt);

  if (isStageFinished(mStages[mCurrentStage])) {
    ++mCurrentStage;

    if (mCurrentStage >= mStages.size()) {
      mCurrentStage = 0;

      if (mFirstRunIncludedStoryAnimation) {
        const auto storyStageIter = mStages.begin() + 2;
        mStages.erase(storyStageIter);
        mFirstRunIncludedStoryAnimation = false;
      }
    }

    startStage(mStages[mCurrentStage]);
  }

  return {};
}

}
