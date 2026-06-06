#include <iostream>
#include <iomanip>
#include <string>
#include <filesystem>
#include <cstdlib>
#include "model/Tournament.h"
#include "model/MonteCarlo.h"
#include "output/AsciiPrinter.h"
#include "output/WebServer.h"

namespace {

const std::string DEFAULT_TEAMS_PATH = "data/teams.csv";
const std::string DEFAULT_SCHEDULE_PATH = "data/schedule.csv";
const std::string DEFAULT_HISTORICAL_PATH = "data/historical/past_world_cups.csv";
const std::string DEFAULT_MODEL_COEFFS_PATH = "data/model_coefficients.csv";

void printUsage() {
    std::cerr << "Usage:\n"
              << "  ./wc status\n"
              << "  ./wc simulate [iterations]\n"
              << "  ./wc impact [iterations]\n"
              << "  ./wc backfit-model\n"
              << "  ./wc fetch-live\n"
              << "  ./wc web [port]\n";
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        std::string command = "status";
        if (argc > 1) {
            command = argv[1];
        }

        std::cout << "=== wc26 World Cup Tracker & Simulator ===" << std::endl << std::endl;

        if (command == "fetch-live") {
            std::string scriptPath = "scripts/fetch_live_scores.py";
            std::string fullCmd = "python3 " + scriptPath + " --schedule " + DEFAULT_SCHEDULE_PATH;
            std::cout << "Invoking live score sync: " << fullCmd << std::endl;
            int status = std::system(fullCmd.c_str());
            if (status != 0) {
                std::cerr << "Error: Sync failed with exit status " << status << std::endl;
                return 1;
            }
            return 0;
        }

        if (command == "backfit-model") {
            std::cout << "Calibrating Poisson Elo goal model from " << DEFAULT_HISTORICAL_PATH << "..." << std::endl;
            auto result = fitPoissonModel(DEFAULT_HISTORICAL_PATH);
            std::cout << "\nCalibration Complete:\n"
                      << "  Base rate (expected goals/team/match): " << std::fixed << std::setprecision(4)
                      << result.baseRate << "\n"
                      << "  Alpha (Elo difference factor): " << result.alpha << "\n"
                      << "  Host Advantage multiplier: " << result.hostAdvantage << "\n"
                      << "  Negative Log-Likelihood (NLL): " << result.nll << "\n";
            
            // Save parameters
            MonteCarlo mc;
            mc.setModelParameters(result.baseRate, result.alpha, result.hostAdvantage);
            mc.saveModelParameters(DEFAULT_MODEL_COEFFS_PATH);
            std::cout << "Saved calibrated parameters to " << DEFAULT_MODEL_COEFFS_PATH << std::endl;
            return 0;
        }

        // Load tournament data
        std::cout << "Loading teams from " << DEFAULT_TEAMS_PATH << "..." << std::endl;
        std::cout << "Loading schedule from " << DEFAULT_SCHEDULE_PATH << "..." << std::endl;
        Tournament tournament = wc::loadTournamentFromCsvFiles(DEFAULT_TEAMS_PATH, DEFAULT_SCHEDULE_PATH);
        std::cout << "Loaded " << tournament.allTeams().size() << " teams and "
                  << tournament.allMatches().size() << " matches." << std::endl << std::endl;

        // Load model parameters if they exist
        MonteCarlo mc;
        if (std::filesystem::exists(DEFAULT_MODEL_COEFFS_PATH)) {
            if (mc.loadModelParameters(DEFAULT_MODEL_COEFFS_PATH)) {
                std::cout << "Loaded model coefficients from " << DEFAULT_MODEL_COEFFS_PATH << "\n"
                          << "  Base rate: " << mc.baseRate() << "\n"
                          << "  Alpha: " << mc.alpha() << "\n"
                          << "  Host Advantage: " << mc.hostAdvantage() << "\n\n";
            }
        } else {
            std::cout << "Model coefficients file not found. Using default parameters.\n\n";
        }

        if (command == "status") {
            std::cout << "Computing group standings..." << std::endl;
            tournament.computeStandings();
            AsciiPrinter::printAllStandings(tournament);
        } else if (command == "simulate") {
            int iterations = 100000;
            if (argc > 2) {
                iterations = std::stoi(argv[2]);
            }
            std::cout << "Running Monte Carlo simulations (" << iterations << " iterations)..." << std::endl;
            auto results = mc.simulate(tournament, iterations, 12345);
            AsciiPrinter::printSimulationResults(results);
        } else if (command == "impact") {
            int iterations = 10000;
            if (argc > 2) {
                iterations = std::stoi(argv[2]);
            }
            std::cout << "Running match importance analysis (" << iterations << " iterations)..." << std::endl;
            auto analysis = mc.analyzeImpact(tournament, iterations, 12345);
            AsciiPrinter::printImpactAnalysis(analysis);
        } else if (command == "web") {
            int port = 8080;
            if (argc > 2) {
                port = std::stoi(argv[2]);
            }
            WebServer server(tournament,
                             DEFAULT_SCHEDULE_PATH,
                             mc.baseRate(),
                             mc.alpha(),
                             mc.hostAdvantage(),
                             20000);
            server.run(port);
        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            printUsage();
            return 1;
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
