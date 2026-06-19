#ifndef ASCII_PRINTER_H
#define ASCII_PRINTER_H

#include "model/Tournament.h"
#include "model/MonteCarlo.h"

class AsciiPrinter {
public:
    static void printAllStandings(const Tournament& tournament, const MatchSimulationResults* simResults = nullptr);
    static void printSimulationResults(const MatchSimulationResults& results);
    static void printImpactAnalysis(const ImpactAnalysisResults& analysis);
};

#endif // ASCII_PRINTER_H
