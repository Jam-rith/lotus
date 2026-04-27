#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_LINEARORDERINGMODEL_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_LINEARORDERINGMODEL_H_

#include "Dataflow/APA/Importance/Dynamic/OrderingFeatures.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace elimination {

// Inference-only linear ordering model for dynamic elimination.
//
// Dynamic ordering is queried repeatedly while the elimination matrix changes.
// Keep this model deliberately cheap: standardized linear regression only, no
// neural layers. Heavier neural models belong to one-shot/static importance
// scoring where each node or region is scored once.
class LinearOrderingModel final {
public:
  bool loadFromFile(const std::string &Path) {
    Loaded = false;
    Intercept = 0.0;
    Weights.clear();
    FeatureMean.clear();
    FeatureStd.clear();
    Needs.UsedFeatures.clear();
    if (Path.empty()) {
      return false;
    }

    std::ifstream In(Path);
    if (!In) {
      return false;
    }
    const std::string Text((std::istreambuf_iterator<char>(In)),
                           std::istreambuf_iterator<char>());

    const auto ModelType = parseString(Text, "model_type");
    if (ModelType.find("mlp") != std::string::npos ||
        ModelType.find("neural") != std::string::npos) {
      return false;
    }

    Intercept = parseScalar(Text, "intercept", 0.0);
    Weights = parseNumericMap(Text, "weights");
    FeatureMean = parseNumericMap(Text, "feature_mean");
    FeatureStd = parseNumericMap(Text, "feature_std");
    auto Features = parseStringArray(Text, "features");
    if (!Features.empty()) {
      for (const auto &Feature : Features) {
        if (Weights.find(Feature) != Weights.end()) {
          Needs.UsedFeatures.insert(Feature);
        }
      }
    } else {
      for (const auto &Entry : Weights) {
        Needs.UsedFeatures.insert(Entry.first);
      }
    }

    Loaded = !Weights.empty();
    return Loaded;
  }

  bool isLoaded() const { return Loaded; }
  const OrderingFeatureNeeds &featureNeeds() const { return Needs; }

  double predict(const OrderingFeatureVector &Features) const {
    if (!Loaded) {
      return 0.0;
    }

    double Score = Intercept;
    for (const auto &Entry : Weights) {
      const auto &Name = Entry.first;
      const auto Raw = orderingFeatureValue(Features, Name);
      const auto MeanIt = FeatureMean.find(Name);
      const auto StdIt = FeatureStd.find(Name);
      const auto Mean = MeanIt == FeatureMean.end() ? 0.0 : MeanIt->second;
      auto Std = StdIt == FeatureStd.end() ? 1.0 : StdIt->second;
      if (std::abs(Std) < 1e-12) {
        Std = 1.0;
      }
      Score += Entry.second * ((Raw - Mean) / Std);
    }
    return std::isfinite(Score) ? Score : 0.0;
  }

private:
  static std::string parseString(const std::string &Text,
                                 const std::string &Name) {
    const std::regex Pattern("\"" + Name + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch Match;
    if (!std::regex_search(Text, Match, Pattern) || Match.size() < 2) {
      return "";
    }
    return Match[1].str();
  }

  static double parseScalar(const std::string &Text, const std::string &Name,
                            double DefaultValue) {
    const std::regex Pattern("\"" + Name +
                             "\"\\s*:\\s*(-?[0-9.eE+\\-]+)");
    std::smatch Match;
    if (!std::regex_search(Text, Match, Pattern) || Match.size() < 2) {
      return DefaultValue;
    }
    try {
      return std::stod(Match[1].str());
    } catch (...) {
      return DefaultValue;
    }
  }

  static std::string extractBalanced(const std::string &Text,
                                     const std::string &Name, char Open,
                                     char Close) {
    const auto Key = "\"" + Name + "\"";
    const auto Start = Text.find(Key);
    if (Start == std::string::npos) {
      return "";
    }
    const auto OpenPos = Text.find(Open, Start);
    if (OpenPos == std::string::npos) {
      return "";
    }

    int Depth = 0;
    for (std::size_t I = OpenPos; I < Text.size(); ++I) {
      if (Text[I] == Open) {
        ++Depth;
      } else if (Text[I] == Close) {
        --Depth;
        if (Depth == 0) {
          return Text.substr(OpenPos, I - OpenPos + 1);
        }
      }
    }
    return "";
  }

  static std::unordered_map<std::string, double>
  parseNumericMap(const std::string &Text, const std::string &Name) {
    std::unordered_map<std::string, double> Result;
    const auto Object = extractBalanced(Text, Name, '{', '}');
    if (Object.empty()) {
      return Result;
    }

    const auto Body = Object.substr(1, Object.size() - 2);
    const std::regex EntryPattern(
        "\"([^\"]+)\"\\s*:\\s*(-?[0-9.eE+\\-]+)");
    for (auto It = std::sregex_iterator(Body.begin(), Body.end(), EntryPattern);
         It != std::sregex_iterator(); ++It) {
      try {
        Result.emplace((*It)[1].str(), std::stod((*It)[2].str()));
      } catch (...) {
      }
    }
    return Result;
  }

  static std::vector<std::string> parseStringArray(const std::string &Text,
                                                   const std::string &Name) {
    std::vector<std::string> Result;
    const auto Array = extractBalanced(Text, Name, '[', ']');
    if (Array.empty()) {
      return Result;
    }
    const std::regex StringPattern("\"([^\"]*)\"");
    for (auto It = std::sregex_iterator(Array.begin(), Array.end(),
                                        StringPattern);
         It != std::sregex_iterator(); ++It) {
      Result.push_back((*It)[1].str());
    }
    return Result;
  }

  bool Loaded = false;
  double Intercept = 0.0;
  std::unordered_map<std::string, double> Weights;
  std::unordered_map<std::string, double> FeatureMean;
  std::unordered_map<std::string, double> FeatureStd;
  OrderingFeatureNeeds Needs;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_LINEARORDERINGMODEL_H_
