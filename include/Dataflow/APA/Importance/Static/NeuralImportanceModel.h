#ifndef DATAFLOW_APA_IMPORTANCE_STATIC_NEURALIMPORTANCEMODEL_H_
#define DATAFLOW_APA_IMPORTANCE_STATIC_NEURALIMPORTANCEMODEL_H_

#include "Dataflow/APA/Importance/Dynamic/OrderingFeatures.h"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace elimination {

// One-shot neural importance model for static/sparse/demand scoring.
//
// Unlike dynamic elimination ordering, static importance features are computed
// once per node/region. Running a small MLP here is acceptable because it does
// not sit inside the per-elimination-step hot path.
class NeuralImportanceModel final {
public:
  bool loadFromFile(const std::string &Path) {
    Loaded = false;
    FeatureMean.clear();
    FeatureStd.clear();
    Features.clear();
    Layers.clear();
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
    if (ModelType.find("mlp") == std::string::npos &&
        ModelType.find("neural") == std::string::npos) {
      return false;
    }

    FeatureMean = parseNumericMap(Text, "feature_mean");
    FeatureStd = parseNumericMap(Text, "feature_std");
    Features = parseStringArray(Text, "features");
    Layers = parseLayers(Text);
    Loaded = !Features.empty() && !Layers.empty();
    return Loaded;
  }

  bool isLoaded() const { return Loaded; }

  double predict(const OrderingFeatureVector &InputFeatures) const {
    if (!Loaded) {
      return 0.0;
    }

    std::vector<double> Activations;
    Activations.reserve(Features.size());
    for (const auto &Name : Features) {
      const auto Raw = orderingFeatureValue(InputFeatures, Name);
      const auto MeanIt = FeatureMean.find(Name);
      const auto StdIt = FeatureStd.find(Name);
      const auto Mean = MeanIt == FeatureMean.end() ? 0.0 : MeanIt->second;
      auto Std = StdIt == FeatureStd.end() ? 1.0 : StdIt->second;
      if (std::abs(Std) < 1e-12) {
        Std = 1.0;
      }
      Activations.push_back((Raw - Mean) / Std);
    }

    for (std::size_t LayerIndex = 0; LayerIndex < Layers.size(); ++LayerIndex) {
      const auto &Layer = Layers[LayerIndex];
      if (Activations.size() != Layer.InputDim) {
        return 0.0;
      }

      std::vector<double> Next(Layer.OutputDim, 0.0);
      for (std::size_t Out = 0; Out < Layer.OutputDim; ++Out) {
        double Value = Layer.Bias[Out];
        for (std::size_t In = 0; In < Layer.InputDim; ++In) {
          Value += Activations[In] *
                   Layer.Weights[Out * Layer.InputDim + In];
        }
        if (LayerIndex + 1 != Layers.size() && Value < 0.0) {
          Value = 0.0;
        }
        Next[Out] = Value;
      }
      Activations = std::move(Next);
    }
    return Activations.empty() || !std::isfinite(Activations[0])
               ? 0.0
               : Activations[0];
  }

private:
  struct Layer final {
    std::size_t InputDim = 0;
    std::size_t OutputDim = 0;
    std::vector<double> Weights;
    std::vector<double> Bias;
  };

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

  static std::vector<double> parseNumericArray(const std::string &Text,
                                               const std::string &Name) {
    std::vector<double> Result;
    const auto Array = extractBalanced(Text, Name, '[', ']');
    if (Array.empty()) {
      return Result;
    }
    const std::regex NumberPattern(
        "-?[0-9]+(?:\\.[0-9]*)?(?:[eE][+\\-]?[0-9]+)?|"
        "-?\\.[0-9]+(?:[eE][+\\-]?[0-9]+)?");
    for (auto It = std::sregex_iterator(Array.begin(), Array.end(),
                                        NumberPattern);
         It != std::sregex_iterator(); ++It) {
      try {
        Result.push_back(std::stod((*It)[0].str()));
      } catch (...) {
      }
    }
    return Result;
  }

  static std::vector<std::string> splitLayerObjects(const std::string &Array) {
    std::vector<std::string> Objects;
    int Depth = 0;
    std::size_t ObjectStart = std::string::npos;
    for (std::size_t I = 0; I < Array.size(); ++I) {
      if (Array[I] == '{') {
        if (Depth == 0) {
          ObjectStart = I;
        }
        ++Depth;
      } else if (Array[I] == '}') {
        --Depth;
        if (Depth == 0 && ObjectStart != std::string::npos) {
          Objects.push_back(Array.substr(ObjectStart, I - ObjectStart + 1));
          ObjectStart = std::string::npos;
        }
      }
    }
    return Objects;
  }

  static std::vector<Layer> parseLayers(const std::string &Text) {
    std::vector<Layer> Result;
    const auto Array = extractBalanced(Text, "layers", '[', ']');
    if (Array.empty()) {
      return Result;
    }

    for (const auto &Object : splitLayerObjects(Array)) {
      Layer L;
      L.InputDim = static_cast<std::size_t>(
          parseScalar(Object, "input_dim", 0.0));
      L.OutputDim = static_cast<std::size_t>(
          parseScalar(Object, "output_dim", 0.0));
      L.Weights = parseNumericArray(Object, "weights");
      L.Bias = parseNumericArray(Object, "bias");
      if (L.InputDim == 0 || L.OutputDim == 0 ||
          L.Weights.size() != L.InputDim * L.OutputDim ||
          L.Bias.size() != L.OutputDim) {
        continue;
      }
      Result.push_back(std::move(L));
    }
    return Result;
  }

  bool Loaded = false;
  std::vector<std::string> Features;
  std::unordered_map<std::string, double> FeatureMean;
  std::unordered_map<std::string, double> FeatureStd;
  std::vector<Layer> Layers;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_STATIC_NEURALIMPORTANCEMODEL_H_
