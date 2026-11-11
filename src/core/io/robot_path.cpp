#include "core/io/robot_path.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace pf::io {
namespace {

std::string fmt(double value, int precision = 2) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    std::string text = stream.str();
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    if (text == "-0") text = "0";
    return text;
}

double wrap_degrees(double angle) {
    while (angle > 180.0) angle -= 360.0;
    while (angle <= -180.0) angle += 360.0;
    return angle;
}

double speed_for(double base_speed, double curvature) {
    const double radius = std::abs(curvature) > 1e-9 ? 1.0 / std::abs(curvature) : 1e9;
    const double factor = std::clamp(radius / 300.0, 0.35, 1.0);
    return std::max(40.0, base_speed * factor);
}

std::string command_for(double delta_deg) {
    if (std::abs(delta_deg) < 1.0) return "AVANCAR";
    if (delta_deg > 0.0) return "VIRAR_DIR";
    return "VIRAR_ESQ";
}

} // namespace

RobotPath build_robot_path(const Scene& scene, const RobotPathOptions& options) {
    RobotPath robot;

    PathTableOptions path_options;
    path_options.step_mm = std::max(5.0, options.step_mm);
    path_options.tolerance_mm = 0.2;
    robot.table = build_path_table(scene, path_options);
    robot.point_count = static_cast<int>(robot.table.samples.size());
    robot.total_length_mm = robot.table.total_length_mm;

    const double base_speed = std::max(10.0, options.speed_mm_s);
    const auto width_for = [&options](const PathSample& sample) {
        return options.brush_width_mm > 0.0 ? options.brush_width_mm : sample.width_mm;
    };

    std::vector<double> deltas(robot.table.samples.size(), 0.0);
    double total_turn = 0.0;
    for (std::size_t i = 1; i < robot.table.samples.size(); ++i) {
        deltas[i] = wrap_degrees(robot.table.samples[i].heading_deg -
                                 robot.table.samples[i - 1].heading_deg);
        total_turn += deltas[i];
    }
    robot.clockwise = total_turn >= 0.0;

    // ------------------------------------------------------------------
    // CSV de pontos e comandos.
    // ------------------------------------------------------------------
    std::ostringstream csv;
    csv << "ordem;comando;x_mm;y_mm;rumo_deg;delta_deg;distancia_mm;largura_mm;"
           "velocidade_mm_s;curvatura_1_mm;elemento\n";

    for (std::size_t i = 0; i < robot.table.samples.size(); ++i) {
        const PathSample& sample = robot.table.samples[i];
        const double distance = i == 0 ? 0.0 : sample.s_mm - robot.table.samples[i - 1].s_mm;
        std::string command;
        if (i == 0) {
            command = "INICIO";
        } else if (i + 1 == robot.table.samples.size()) {
            command = "FIM";
        } else {
            command = command_for(deltas[i]);
        }

        csv << (i + 1) << ';' << command << ';' << fmt(sample.position.x, 3) << ';'
            << fmt(sample.position.y, 3) << ';' << fmt(sample.heading_deg, 2) << ';'
            << fmt(deltas[i], 2) << ';' << fmt(distance, 2) << ';'
            << fmt(width_for(sample), 2) << ';'
            << fmt(speed_for(base_speed, sample.curvature), 1) << ';'
            << fmt(sample.curvature, 6) << ';' << sample.element << '\n';
    }
    robot.points_csv = csv.str();

    // ------------------------------------------------------------------
    // Instruções legíveis.
    // ------------------------------------------------------------------
    std::ostringstream text;
    text << "PISTAFORGE - INSTRUCOES DE PINTURA\n";
    text << "==================================\n\n";
    text << "Pista: " << scene.arena.name << '\n';
    text << "Arena: " << fmt(scene.arena.width_mm, 0) << " x "
         << fmt(scene.arena.height_mm, 0) << " mm\n";
    text << "Comprimento da linha central: " << fmt(robot.total_length_mm, 1) << " mm\n";
    text << "Pontos: " << robot.point_count << " (passo " << fmt(robot.table.step_mm, 1)
         << " mm)\n";
    text << "Sentido: " << (robot.clockwise ? "horario" : "anti-horario") << '\n';
    text << "Largura nominal: "
         << fmt(options.brush_width_mm > 0.0 ? options.brush_width_mm : 19.0, 1) << " mm\n";
    text << "Velocidade base: " << fmt(base_speed, 0) << " mm/s\n\n";

    if (!robot.table.samples.empty()) {
        const PathSample& first = robot.table.samples.front();
        const PathSample& last = robot.table.samples.back();
        text << "Inicio: x=" << fmt(first.position.x, 1) << " mm, y=" << fmt(first.position.y, 1)
             << " mm, rumo " << fmt(first.heading_deg, 1) << " graus\n";
        text << "Fim:    x=" << fmt(last.position.x, 1) << " mm, y=" << fmt(last.position.y, 1)
             << " mm, rumo " << fmt(last.heading_deg, 1) << " graus\n";
        text << "Pista fechada: " << (robot.table.closed ? "sim" : "nao") << "\n\n";
    }

    text << "TABELA DE COMANDOS\n";
    text << "------------------\n";
    text << "#   COMANDO     X(mm)    Y(mm)   RUMO    DIST    LARG    VEL\n";
    for (std::size_t i = 0; i < robot.table.samples.size(); ++i) {
        const PathSample& sample = robot.table.samples[i];
        const double distance = i == 0 ? 0.0 : sample.s_mm - robot.table.samples[i - 1].s_mm;
        const std::string command = i == 0
                                        ? "INICIO"
                                        : (i + 1 == robot.table.samples.size()
                                               ? "FIM"
                                               : command_for(deltas[i]));
        text << std::left << std::setw(4) << (i + 1) << std::setw(12) << command << std::right
             << std::setw(9) << fmt(sample.position.x, 1) << std::setw(9)
             << fmt(sample.position.y, 1) << std::setw(8) << fmt(sample.heading_deg, 1)
             << std::setw(8) << fmt(distance, 1) << std::setw(8) << fmt(width_for(sample), 1)
             << std::setw(8) << fmt(speed_for(base_speed, sample.curvature), 0) << '\n';
    }
    text << '\n';

    text << "SEQUENCIA PASSO A PASSO\n";
    text << "-----------------------\n";
    for (std::size_t i = 0; i < robot.table.samples.size(); ++i) {
        const PathSample& sample = robot.table.samples[i];
        text << (i + 1) << ". ";
        if (i == 0) {
            text << "Posicionar o robo em x=" << fmt(sample.position.x, 1)
                 << " mm, y=" << fmt(sample.position.y, 1) << " mm com rumo "
                 << fmt(sample.heading_deg, 1) << " graus. Baixar o pincel.";
        } else if (i + 1 == robot.table.samples.size()) {
            text << "Chegar a x=" << fmt(sample.position.x, 1) << " mm, y="
                 << fmt(sample.position.y, 1) << " mm e levantar o pincel. Fim da pintura.";
        } else {
            const double distance = sample.s_mm - robot.table.samples[i - 1].s_mm;
            if (std::abs(deltas[i]) < 1.0) {
                text << "Avancar " << fmt(distance, 1) << " mm ate x="
                     << fmt(sample.position.x, 1) << " mm, y=" << fmt(sample.position.y, 1)
                     << " mm.";
            } else if (deltas[i] > 0.0) {
                text << "Virar a direita " << fmt(std::abs(deltas[i]), 1) << " graus e avancar "
                     << fmt(distance, 1) << " mm ate x=" << fmt(sample.position.x, 1)
                     << " mm, y=" << fmt(sample.position.y, 1) << " mm.";
            } else {
                text << "Virar a esquerda " << fmt(std::abs(deltas[i]), 1)
                     << " graus e avancar " << fmt(distance, 1) << " mm ate x="
                     << fmt(sample.position.x, 1) << " mm, y=" << fmt(sample.position.y, 1)
                     << " mm.";
            }
        }
        text << '\n';
    }

    text << "\nOBSERVACOES\n";
    text << "-----------\n";
    text << "- Mantenha o pincel abaixado do primeiro ao ultimo ponto.\n";
    text << "- A largura indicada e a faixa nominal; ajuste o bico do pincel conforme o teste.\n";
    text << "- A velocidade sugerida diminui nas curvas (raio menor = mais devagar).\n";
    text << "- Use a coluna 'distancia_mm' para conferir o avanco entre pontos.\n";

    robot.instructions = text.str();
    return robot;
}

} // namespace pf::io
