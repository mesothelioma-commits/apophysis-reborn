#include "legacy_script.hpp"
#include "plugin.hpp"
#include "script.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace apo::detail {
namespace {
struct Token {
  std::string text;
  std::size_t line = 1;
  enum Kind { identifier, number, string, symbol, end } kind = symbol;
};
std::string lower(std::string s) {
  for (auto &c : s)
    if (c >= 'A' && c <= 'Z')
      c = char(c - 'A' + 'a');
  return s;
}
std::string quote(std::string_view s) {
  std::string result = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      result += '\\';
      result += char(c);
    } else if (c == '\n')
      result += "\\n";
    else if (c == '\r')
      result += "\\r";
    else if (c == '\t')
      result += "\\t";
    else if (c < 32) {
      const auto n = std::to_string(unsigned(c));
      result += '\\';
      result += std::string(3 - n.size(), '0') + n;
    } else
      result += char(c);
  }
  return result + '"';
}
[[noreturn]] void fail(const Token &t, const std::string &message) {
  throw ScriptError(
      "Legacy script line " + std::to_string(t.line) + ": " + message, t.line);
}
std::vector<Token> lex(std::string_view source,
                       const std::function<void()> &check) {
  std::vector<Token> out;
  std::size_t p = 0, line = 1;
  auto step = [&] {
    if (source[p] == '\n')
      ++line;
    ++p;
  };
  while (p < source.size()) {
    check();
    const unsigned char c = source[p];
    if (std::isspace(c)) {
      step();
      continue;
    }
    if (c == '{' ||
        (c == '(' && p + 1 < source.size() && source[p + 1] == '*')) {
      const auto begin = line;
      const bool brace = c == '{';
      p += brace ? 1 : 2;
      bool closed = false;
      while (p < source.size()) {
        if ((brace && source[p] == '}') ||
            (!brace && source[p] == '*' && p + 1 < source.size() &&
             source[p + 1] == ')')) {
          p += brace ? 1 : 2;
          closed = true;
          break;
        }
        step();
        if ((p & 1023) == 0)
          check();
      }
      if (!closed)
        fail(Token{"", begin}, "Unterminated comment");
      continue;
    }
    if (c == '/' && p + 1 < source.size() && source[p + 1] == '/') {
      while (p < source.size() && source[p] != '\n')
        ++p;
      continue;
    }
    Token t;
    t.line = line;
    if (c == '\'') {
      t.kind = Token::string;
      ++p;
      bool closed = false;
      while (p < source.size()) {
        if (source[p] == '\'') {
          ++p;
          if (p < source.size() && source[p] == '\'') {
            t.text += '\'';
            ++p;
            continue;
          }
          closed = true;
          break;
        }
        if (source[p] == '\n' || source[p] == '\r')
          fail(t, "A Pascal string cannot span source lines");
        t.text += source[p++];
      }
      if (!closed)
        fail(t, "Unterminated string");
    } else if (std::isalpha(c) || c == '_') {
      t.kind = Token::identifier;
      while (p < source.size() &&
             (std::isalnum(static_cast<unsigned char>(source[p])) ||
              source[p] == '_'))
        t.text += source[p++];
      if (t.text.size() > 128)
        fail(t, "Identifier is too long");
      t.text = lower(t.text);
    } else if (std::isdigit(c)) {
      t.kind = Token::number;
      const auto start = p;
      while (p < source.size() &&
             std::isdigit(static_cast<unsigned char>(source[p])))
        ++p;
      if (p + 1 < source.size() && source[p] == '.' &&
          std::isdigit(static_cast<unsigned char>(source[p + 1]))) {
        ++p;
        while (p < source.size() &&
               std::isdigit(static_cast<unsigned char>(source[p])))
          ++p;
      }
      if (p < source.size() && (source[p] == 'e' || source[p] == 'E')) {
        ++p;
        if (p < source.size() && (source[p] == '+' || source[p] == '-'))
          ++p;
        const auto digits = p;
        while (p < source.size() &&
               std::isdigit(static_cast<unsigned char>(source[p])))
          ++p;
        if (digits == p)
          fail(t, "Malformed exponent");
      }
      t.text = std::string(source.substr(start, p - start));
    } else {
      t.kind = Token::symbol;
      t.text = char(c);
      ++p;
      if (p < source.size() &&
          ((c == ':' && source[p] == '=') ||
           (c == '<' && (source[p] == '>' || source[p] == '=')) ||
           (c == '>' && source[p] == '=')))
        t.text += source[p++];
      const std::set<std::string> valid{";", ":", ":=", ".",  ",", "(", ")",
                                        "[", "]", "+",  "-",  "*", "/", "=",
                                        "<", ">", "<=", ">=", "<>"};
      if (!valid.contains(t.text))
        fail(t, "Unsupported token '" + t.text + "'");
    }
    out.push_back(std::move(t));
    if (out.size() > 100000)
      fail(out.back(), "Too many tokens");
  }
  out.push_back(Token{"", line, Token::end});
  return out;
}
struct Access {
  std::string name;
  std::vector<std::string> indices;
  Token token;
};
class Parser {
  std::vector<Token> tokens_;
  std::size_t pos_ = 0, depth_ = 0;
  std::size_t output_limit_;
  const std::function<void()> &check_;
  LegacyTranslation result_;
  std::set<std::string> assigned_, used_, constants_, loop_variables_;
  std::size_t loop_serial_ = 0;
  std::map<std::string, std::string> declared_types_;
  std::vector<std::pair<std::string, std::string>> initializers_;
  const Token &peek() const { return tokens_.at(pos_); }
  bool is(std::string_view s) const { return peek().text == s; }
  bool match(std::string_view s) {
    if (is(s)) {
      ++pos_;
      check_();
      return true;
    }
    return false;
  }
  Token take() {
    check_();
    return tokens_.at(pos_++);
  }
  void expect(std::string_view s) {
    if (!match(s))
      fail(peek(), "Expected '" + std::string(s) + "'");
  }
  Token identifier() {
    const auto t = take();
    if (t.kind != Token::identifier)
      fail(t, "Expected an identifier");
    return t;
  }
  static std::string variable(const std::string &s) { return "v_" + s; }
  void emit(const std::string &s, std::size_t line) {
    if (result_.source.size() + s.size() + 1 > output_limit_)
      fail(peek(), "Translation output limit exceeded");
    result_.source += s + '\n';
    result_.source_lines.push_back(line);
  }
  static std::string call(const std::string &op, const std::string &name,
                          const std::vector<std::string> &args = {}) {
    std::string out = "__apo_legacy(" + quote(op) + "," + quote(name);
    for (const auto &arg : args)
      out += "," + arg;
    return out + ")";
  }
  static std::string truth(const std::string &expr) {
    return call("truth", "condition", {expr});
  }
  bool property(const Access &a, bool write) const {
    static const std::set<std::string> globals{"activetransform", "transforms",
                                               "filecount",       "nvars",
                                               "nxforms",         "stopped"};
    if (globals.contains(a.name)) {
      if (!a.indices.empty())
        fail(a.token, "This property has no indices");
      if (write && a.name != "activetransform")
        fail(a.token, "Read-only legacy property: " + a.name);
      return true;
    }
    static const std::map<std::string, int> props{{"flame.name", 0},
                                                  {"flame.gamma", 0},
                                                  {"flame.brightness", 0},
                                                  {"flame.vibrancy", 0},
                                                  {"flame.time", 0},
                                                  {"flame.zoom", 0},
                                                  {"flame.x", 0},
                                                  {"flame.y", 0},
                                                  {"flame.width", 0},
                                                  {"flame.height", 0},
                                                  {"flame.sampledensity", 0},
                                                  {"flame.quality", 0},
                                                  {"flame.oversample", 0},
                                                  {"flame.filterradius", 0},
                                                  {"flame.scale", 0},
                                                  {"flame.angle", 0},
                                                  {"flame.gradient", 2},
                                                  {"flame.background", 1},
                                                  {"transform.a", 0},
                                                  {"transform.b", 0},
                                                  {"transform.c", 0},
                                                  {"transform.d", 0},
                                                  {"transform.e", 0},
                                                  {"transform.f", 0},
                                                  {"transform.coefs", 2},
                                                  {"transform.post", 2},
                                                  {"transform.weight", 0},
                                                  {"transform.color", 0},
                                                  {"transform.symmetry", 0},
                                                  {"transform.plotmode", 0},
                                                  {"transform.variation", 1},
                                                  {"transform.chaos", 1},
                                                  {"transform.julian_power", 0},
                                                  {"transform.julian_dist", 0},
                                                  {"renderer.filename", 0},
                                                  {"renderer.width", 0},
                                                  {"renderer.height", 0},
                                                  {"renderer.maxmemory", 0}};
    auto it = props.find(a.name);
    if (it != props.end()) {
      if (a.indices.size() != static_cast<std::size_t>(it->second))
        fail(a.token, "Incorrect index count for " + a.name);
      return true;
    }
    if (a.name.starts_with("transform.") && find_variation(a.name.substr(10))) {
      if (!a.indices.empty())
        fail(a.token, "Named variations have no indices");
      return true;
    }
    if (a.name.find('.') != std::string::npos || !a.indices.empty())
      fail(a.token, "Unsupported legacy property: " + a.name);
    return false;
  }
  Access access() {
    Access a;
    a.token = identifier();
    a.name = a.token.text;
    while (match("."))
      a.name += '.' + identifier().text;
    if (match("[")) {
      a.indices.push_back(expression());
      while (match(","))
        a.indices.push_back(expression());
      expect("]");
    }
    return a;
  }
  std::vector<std::string> arguments() {
    std::vector<std::string> args;
    if (match("(")) {
      if (!match(")")) {
        args.push_back(expression());
        while (match(",")) {
          if (args.size() >= 8)
            fail(peek(), "Too many arguments");
          args.push_back(expression());
        }
        expect(")");
      }
    }
    return args;
  }
  std::string function(const Access &a, const std::vector<std::string> &args,
                       bool statement) {
    if (!statement && args.empty() &&
        (a.name == "transforms" || a.name == "filecount" || a.name == "nvars" ||
         a.name == "nxforms" || a.name == "stopped"))
      return call("get", a.name);
    static const std::map<std::string, std::pair<int, std::string>> math{
        {"sin", {1, "math.sin"}},      {"cos", {1, "math.cos"}},
        {"tan", {1, "math.tan"}},      {"arctan", {1, "math.atan"}},
        {"arctan2", {2, "math.atan"}}, {"abs", {1, "math.abs"}},
        {"sqrt", {1, "math.sqrt"}},    {"exp", {1, "math.exp"}},
        {"ln", {1, "math.log"}},       {"min", {2, "math.min"}},
        {"max", {2, "math.max"}}};
    if (const auto it = math.find(a.name); it != math.end()) {
      if (statement)
        fail(a.token, "Function result cannot be used as a statement");
      if (args.size() != static_cast<std::size_t>(it->second.first))
        fail(a.token, "Incorrect argument count for " + a.name);
      std::string result = it->second.second + '(';
      for (std::size_t i = 0; i < args.size(); ++i) {
        if (i)
          result += ',';
        result += args[i];
      }
      return result + ')';
    }
    static const std::map<std::string, int> expression_calls{
        {"random", 0},   {"variationindex", 1}, {"variationname", 1},
        {"trunc", 1},    {"round", 1},          {"frac", 1},
        {"sqr", 1},      {"inttostr", 1},       {"floattostr", 1},
        {"strtoint", 1}, {"strtofloat", 1}};
    static const std::map<std::string, int> procedures{
        {"clear", 0},
        {"addtransform", 0},
        {"deletetransform", 0},
        {"copytransform", 0},
        {"setactivetransform", 1},
        {"rotate", 1},
        {"scale", 1},
        {"translate", 2},
        {"multiply", 4},
        {"normalizevars", 0},
        {"randomgradient", 0},
        {"print", 1},
        {"showstatus", 1},
        {"render", 0},
        {"saveflame", 1},
        {"loadflame", 1},
        {"transform.clear", 0}};
    const auto &allowed = statement ? procedures : expression_calls;
    const auto it = allowed.find(a.name);
    if (it == allowed.end())
      fail(a.token, "Unsupported legacy " +
                        std::string(statement ? "procedure: " : "function: ") +
                        a.name + " (see LEGACY_SCRIPTING.md)");
    if (!a.indices.empty() ||
        args.size() != static_cast<std::size_t>(it->second))
      fail(a.token, "Incorrect argument/index count for " + a.name);
    return call("call", a.name, args);
  }
  std::string primary() {
    if (++depth_ > 64)
      fail(peek(), "Expression nesting limit exceeded");
    struct Depth {
      std::size_t &d;
      ~Depth() { --d; }
    } depth{depth_};
    if (match("(")) {
      const auto result = expression();
      expect(")");
      return '(' + result + ')';
    }
    if (match("-"))
      return "(-" + primary() + ")";
    if (match("+"))
      return '(' + primary() + ')';
    if (match("not"))
      return "(not " + truth(primary()) + ")";
    if (peek().kind == Token::number)
      return take().text;
    if (peek().kind == Token::string)
      return quote(take().text);
    if (match("true"))
      return "true";
    if (match("false"))
      return "false";
    if (match("pi"))
      return "math.pi";
    if (peek().kind != Token::identifier)
      fail(peek(), "Expected an expression");
    const auto a = access();
    if (is("("))
      return function(a, arguments(), false);
    if (property(a, false))
      return call("get", a.name, a.indices);
    if (a.name == "random")
      return function(a, {}, false);
    used_.insert(a.name);
    return variable(a.name);
  }
  static int precedence(const std::string &op) {
    if (op == "or")
      return 1;
    if (op == "and")
      return 2;
    if (op == "=" || op == "<>" || op == "<" || op == ">" || op == "<=" ||
        op == ">=")
      return 3;
    if (op == "+" || op == "-")
      return 4;
    if (op == "*" || op == "/" || op == "div" || op == "mod")
      return 5;
    return 0;
  }
  std::string expression(int min = 1) {
    auto left = primary();
    while (precedence(peek().text) >= min) {
      const auto op = take().text;
      const auto right = expression(precedence(op) + 1);
      if (op == "+" || op == "div" || op == "mod")
        left = call("binary", op, {left, right});
      else if (op == "and" || op == "or")
        left = '(' + truth(left) + ' ' + op + ' ' + truth(right) + ')';
      else
        left = '(' + left + ' ' +
               (op == "="    ? "=="
                : op == "<>" ? "~="
                             : op) +
               ' ' + right + ')';
      if (left.size() > output_limit_)
        fail(peek(), "Expression output limit exceeded");
    }
    return left;
  }
  std::string coerced(const std::string &name, const std::string &value) const {
    const auto it = declared_types_.find(name);
    return it == declared_types_.end() ? value
                                       : call("coerce", it->second, {value});
  }
  void assignment(const Access &a, const std::string &value) {
    if (property(a, true)) {
      auto args = a.indices;
      args.push_back(value);
      emit(call("set", a.name, args), a.token.line);
    } else {
      if (constants_.contains(a.name))
        fail(a.token, "Cannot assign a constant");
      if (loop_variables_.contains(a.name))
        fail(a.token, "Cannot assign an active for-loop variable");
      assigned_.insert(a.name);
      emit(variable(a.name) + '=' + coerced(a.name, value), a.token.line);
    }
  }
  void sequence(const std::set<std::string> &endings) {
    while (peek().kind != Token::end && !endings.contains(peek().text)) {
      statement();
      if (!match(";") && !endings.contains(peek().text) &&
          peek().kind != Token::end)
        fail(peek(), "Expected ';'");
      while (match(";")) {
      };
    }
  }
  void statement() {
    if (++depth_ > 64)
      fail(peek(), "Statement nesting limit exceeded");
    struct Depth {
      std::size_t &d;
      ~Depth() { --d; }
    } depth{depth_};
    const auto line = peek().line;
    if (match("begin")) {
      sequence({"end"});
      expect("end");
      return;
    }
    if (match("if")) {
      const auto cond = expression();
      expect("then");
      emit("if " + truth(cond) + " then", line);
      statement();
      if (match("else")) {
        emit("else", peek().line);
        statement();
      }
      emit("end", line);
      return;
    }
    if (match("for")) {
      const auto var = identifier();
      if (constants_.contains(var.text) || loop_variables_.contains(var.text))
        fail(var, "Invalid active for-loop variable");
      assigned_.insert(var.text);
      expect(":=");
      const auto first = expression();
      const bool down = match("downto");
      if (!down)
        expect("to");
      const auto last = expression();
      expect("do");
      const auto counter = "__apo_for_" + std::to_string(loop_serial_++);
      emit("for " + counter + "=" + first + "," + last + (down ? ",-1" : "") +
               " do",
           line);
      emit(variable(var.text) + '=' + coerced(var.text, counter), line);
      loop_variables_.insert(var.text);
      statement();
      loop_variables_.erase(var.text);
      emit("end", line);
      return;
    }
    if (match("while")) {
      const auto cond = expression();
      expect("do");
      emit("while " + truth(cond) + " do", line);
      statement();
      emit("end", line);
      return;
    }
    if (match("repeat")) {
      emit("repeat", line);
      sequence({"until"});
      expect("until");
      emit("until " + truth(expression()), line);
      return;
    }
    if (is("end") || is("else") || is("until") || is(";"))
      return;
    if (is("inc") || is("dec")) {
      const auto op = take();
      expect("(");
      const auto target = access();
      std::string step = "1";
      if (match(","))
        step = expression();
      expect(")");
      std::string previous;
      if (property(target, false))
        previous = call("get", target.name, target.indices);
      else {
        used_.insert(target.name);
        previous = variable(target.name);
      }
      assignment(target,
                 '(' + previous + (op.text == "inc" ? "+" : "-") + step + ')');
      return;
    }
    const auto a = access();
    if (match(":=")) {
      assignment(a, expression());
      return;
    }
    emit(function(a, arguments(), true), line);
  }
  void declarations() {
    while (is("var") || is("const")) {
      const bool constant = match("const");
      if (!constant)
        expect("var");
      do {
        std::vector<Token> names{identifier()};
        while (match(","))
          names.push_back(identifier());
        std::string init = "0", declared_type = "variant";
        if (constant) {
          if (names.size() != 1)
            fail(names[0], "Constants need individual declarations");
          expect("=");
          init = expression();
        } else {
          expect(":");
          const auto type = identifier();
          declared_type = type.text;
          if (type.text == "string")
            init = "\"\"";
          else if (type.text == "boolean")
            init = "false";
          else if (type.text != "integer" && type.text != "double" &&
                   type.text != "real" && type.text != "extended" &&
                   type.text != "variant")
            fail(type, "Unsupported variable type: " + type.text);
        }
        expect(";");
        for (const auto &name : names) {
          if (assigned_.contains(name.text))
            fail(name, "Duplicate declaration");
          assigned_.insert(name.text);
          if (constant)
            constants_.insert(name.text);
          else
            declared_types_[name.text] = declared_type;
          initializers_.emplace_back(name.text, init);
        }
      } while (peek().kind == Token::identifier && !is("begin") && !is("var") &&
               !is("const"));
    }
  }

public:
  Parser(std::string_view source, const std::function<void()> &check)
      : tokens_(lex(source, check)), output_limit_(source.size() * 48 + 4096),
        check_(check) {}
  LegacyTranslation run() {
    if (match("program")) {
      identifier();
      expect(";");
    }
    declarations();
    if (match("begin")) {
      sequence({"end"});
      expect("end");
      match(";");
    } else
      sequence({"."});
    match(".");
    if (peek().kind != Token::end)
      fail(peek(), "Unexpected token after the program");
    for (const auto &name : used_)
      if (!assigned_.contains(name))
        fail(tokens_[0], "Unknown legacy variable or property: " + name);
    if (assigned_.size() > 180)
      fail(tokens_[0], "Too many local variables");
    std::string prefix;
    for (const auto &name : assigned_)
      prefix += "local " + variable(name) + "=0;";
    prefix += '\n';
    for (const auto &[name, value] : initializers_)
      prefix += variable(name) + '=' + value + ';';
    prefix += '\n';
    result_.source = prefix + result_.source;
    result_.source_lines.insert(result_.source_lines.begin(), 2, 1);
    result_.notes = {
        "Strict Pascal subset audited against Apophysis 2.09 ScriptForm.pas "
        "and XForm.pas; unsupported syntax and APIs are rejected before "
        "execution.",
        "Legacy transform, library and palette indices are zero-based. "
        "Flame.Scale is percent-of-width; Flame.Angle uses radians.",
        "Random uses Reborn's seeded MT19937-64 stream; it does not reproduce "
        "Delphi's historical random stream. Render/SaveFlame create deferred "
        "requests only."};
    return std::move(result_);
  }
};
} // namespace
LegacyTranslation translate_legacy_script(std::string_view source,
                                          const std::function<void()> &check) {
  return Parser(source, check).run();
}
} // namespace apo::detail
