// ti_host_tokenizer.cpp — TI-99/4A BASIC source ↔ token translation.
//
// Adopted from sunton's newer tokenizer (the one that supports
// TOK_LINENUM after GOTO/GOSUB/THEN, IMAGE literal capture, `!` tail
// comments, and unquoted DSK1.NAME file specs after OLD/SAVE/MERGE/
// DELETE). Box's older version is dropped in favor of this one.
//
// Public entry points (declared in ti_host.h):
//   initTokenNames()   — populates tokenNames[] once at boot
//   tokenizeLine()     — text -> tokens (returns byte count or -1)
//   detokenizeLine()   — tokens -> text (returns byte count)
//
// Everything else in this file is file-local: the keyword table,
// tokenNames[] table, and the small helpers matchKeyword / skipSpaces
// / isLineRefToken / isFilenameKwToken.

#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "ti_host.h"
#include "tp_types.h"  // Token enum + TOK_* constants

namespace tihost
{

// ---------------------------------------------------------------------------
// Keyword table — maps text to tokens (used by tokenizer)
// ---------------------------------------------------------------------------

struct KeywordEntry
{
  const char* text;
  Token token;
};

// Token -> canonical text. Indexed directly by token value (0-255).
//
//   0x00        -> NULL (TOK_EOL, no output)
//   0x01-0x09   -> immediate-command names (CONTINUE, LIST, ...)
//   0x0A-0x1F   -> NULL (control chars, shouldn't appear in the stream)
//   0x20-0x7F   -> single-character strings (raw ASCII — variable names,
//                  digits, punctuation)
//   0x80-0xFF   -> keyword / function / operator names, or NULL for
//                  tokens handled specially (literals, !-comments) or
//                  unused token values
//
// Populated once at startup by initTokenNames(). The single-char strings
// for the ASCII range live in asciiOneChar[].
static const char* tokenNames[256] = { NULL };
static char asciiOneChar[128][2];

void initTokenNames()
{
  // Fill the printable-ASCII range with one-char strings so identifiers
  // and punctuation bytes in the token stream round-trip directly.
  for (int i = 0x20; i < 0x80; i++)
  {
    asciiOneChar[i][0] = (char)i;
    asciiOneChar[i][1] = '\0';
    tokenNames[i] = asciiOneChar[i];
  }

  // Immediate commands 0x00-0x09
  tokenNames[TOK_CONTINUE] = "CONTINUE";
  tokenNames[TOK_LIST]     = "LIST";
  tokenNames[TOK_BYE]      = "BYE";
  tokenNames[TOK_NUM]      = "NUMBER";
  tokenNames[TOK_OLD]      = "OLD";
  tokenNames[TOK_RES]      = "RESEQUENCE";
  tokenNames[TOK_SAVE]     = "SAVE";
  tokenNames[TOK_MERGE]    = "MERGE";
  tokenNames[TOK_EDIT]     = "EDIT";

  // Statements 0x81-0xAA
  tokenNames[TOK_ELSE]     = "ELSE";
  tokenNames[TOK_DCOLON]   = "::";
  tokenNames[TOK_BANG]     = "!";
  tokenNames[TOK_IF]       = "IF";
  tokenNames[TOK_GO]       = "GO";
  tokenNames[TOK_GOTO]     = "GOTO";
  tokenNames[TOK_GOSUB]    = "GOSUB";
  tokenNames[TOK_RETURN]   = "RETURN";
  tokenNames[TOK_DEF]      = "DEF";
  tokenNames[TOK_DIM]      = "DIM";
  tokenNames[TOK_END]      = "END";
  tokenNames[TOK_FOR]      = "FOR";
  tokenNames[TOK_LET]      = "LET";
  tokenNames[TOK_BREAK]    = "BREAK";
  tokenNames[TOK_UNBREAK]  = "UNBREAK";
  tokenNames[TOK_TRACE]    = "TRACE";
  tokenNames[TOK_UNTRACE]  = "UNTRACE";
  tokenNames[TOK_INPUT]    = "INPUT";
  tokenNames[TOK_DATA]     = "DATA";
  tokenNames[TOK_RESTORE]  = "RESTORE";
  tokenNames[TOK_RANDOMIZE]= "RANDOMIZE";
  tokenNames[TOK_NEXT]     = "NEXT";
  tokenNames[TOK_READ]     = "READ";
  tokenNames[TOK_STOP]     = "STOP";
  tokenNames[TOK_DELETE]   = "DELETE";
  tokenNames[TOK_REM]      = "REM";
  tokenNames[TOK_ON]       = "ON";
  tokenNames[TOK_PRINT]    = "PRINT";
  tokenNames[TOK_CALL]     = "CALL";
  tokenNames[TOK_OPTION]   = "OPTION";
  tokenNames[TOK_OPEN]     = "OPEN";
  tokenNames[TOK_CLOSE]    = "CLOSE";
  tokenNames[TOK_SUB]      = "SUB";
  tokenNames[TOK_DISPLAY]  = "DISPLAY";
  tokenNames[TOK_IMAGE]    = "IMAGE";
  tokenNames[TOK_ACCEPT]   = "ACCEPT";
  tokenNames[TOK_ERROR]    = "ERROR";
  tokenNames[TOK_WARNING]  = "WARNING";
  tokenNames[TOK_SUBEXIT]  = "SUBEXIT";
  tokenNames[TOK_SUBEND]   = "SUBEND";
  tokenNames[TOK_RUN]      = "RUN";
  tokenNames[TOK_LINPUT]   = "LINPUT";

  // Secondary keywords + punctuation 0xB0-0xB8
  tokenNames[TOK_THEN]      = "THEN";
  tokenNames[TOK_TO]        = "TO";
  tokenNames[TOK_STEP]      = "STEP";
  tokenNames[TOK_COMMA]     = ",";
  tokenNames[TOK_SEMICOLON] = ";";
  tokenNames[TOK_COLON]     = ":";
  tokenNames[TOK_RPAREN]    = ")";
  tokenNames[TOK_LPAREN]    = "(";
  tokenNames[TOK_CONCAT]    = "&";

  // Operators 0xBA-0xC5
  tokenNames[TOK_OR]       = "OR";
  tokenNames[TOK_AND]      = "AND";
  tokenNames[TOK_XOR]      = "XOR";
  tokenNames[TOK_NOT]      = "NOT";
  tokenNames[TOK_EQUAL]    = "=";
  tokenNames[TOK_LESS]     = "<";
  tokenNames[TOK_GREATER] = ">";
  tokenNames[TOK_PLUS]     = "+";
  tokenNames[TOK_MINUS]    = "-";
  tokenNames[TOK_MULTIPLY] = "*";
  tokenNames[TOK_DIVIDE]   = "/";
  tokenNames[TOK_POWER]    = "^";

  // Functions 0xCB-0xE1
  tokenNames[TOK_ABS] = "ABS"; tokenNames[TOK_ATN] = "ATN";
  tokenNames[TOK_COS] = "COS"; tokenNames[TOK_EXP] = "EXP";
  tokenNames[TOK_INT] = "INT"; tokenNames[TOK_LOG] = "LOG";
  tokenNames[TOK_SGN] = "SGN"; tokenNames[TOK_SIN] = "SIN";
  tokenNames[TOK_SQR] = "SQR"; tokenNames[TOK_TAN] = "TAN";
  tokenNames[TOK_LEN] = "LEN"; tokenNames[TOK_CHR] = "CHR$";
  tokenNames[TOK_RND] = "RND"; tokenNames[TOK_SEG] = "SEG$";
  tokenNames[TOK_POS] = "POS"; tokenNames[TOK_VAL] = "VAL";
  tokenNames[TOK_STR] = "STR$";tokenNames[TOK_ASC] = "ASC";
  tokenNames[TOK_PI]  = "PI";  tokenNames[TOK_REC] = "REC";
  tokenNames[TOK_MAX] = "MAX"; tokenNames[TOK_MIN] = "MIN";
  tokenNames[TOK_RPT] = "RPT$";

  // Extended BASIC keywords 0xE8-0xFE
  tokenNames[TOK_NUMERIC]     = "NUMERIC";
  tokenNames[TOK_DIGIT]       = "DIGIT";
  tokenNames[TOK_UALPHA]      = "UALPHA";
  tokenNames[TOK_SIZE]        = "SIZE";
  tokenNames[TOK_ALL]         = "ALL";
  tokenNames[TOK_USING]       = "USING";
  tokenNames[TOK_BEEP]        = "BEEP";
  tokenNames[TOK_ERASE]       = "ERASE";
  tokenNames[TOK_AT]          = "AT";
  tokenNames[TOK_BASE]        = "BASE";
  tokenNames[TOK_VARIABLE_KW] = "VARIABLE";
  tokenNames[TOK_RELATIVE]    = "RELATIVE";
  tokenNames[TOK_INTERNAL]    = "INTERNAL";
  tokenNames[TOK_SEQUENTIAL]  = "SEQUENTIAL";
  tokenNames[TOK_OUTPUT]      = "OUTPUT";
  tokenNames[TOK_UPDATE]      = "UPDATE";
  tokenNames[TOK_APPEND]      = "APPEND";
  tokenNames[TOK_FIXED]       = "FIXED";
  tokenNames[TOK_PERMANENT]   = "PERMANENT";
  tokenNames[TOK_TAB]         = "TAB";
  tokenNames[TOK_HASH]        = "#";
  tokenNames[TOK_VALIDATE]    = "VALIDATE";
}

// Tokenizer keyword table — text -> token, including aliases.
// Used only for matching source text during tokenization.
static const KeywordEntry keywords[] =
{
  // RUN, NEW, DIR are handled as pre-tokenize string commands
  // (so they're not listed here)
  {"LIST",       TOK_LIST},
  {"OLD",        TOK_OLD},
  {"SAVE",       TOK_SAVE},
  {"BYE",        TOK_BYE},
  {"NUMBER",     TOK_NUM},
  {"NUM",        TOK_NUM},
  {"PRINT",      TOK_PRINT},
  {"USING",      TOK_USING},
  {"DISPLAY",    TOK_DISPLAY},
  {"ACCEPT",     TOK_ACCEPT},
  {"GOTO",       TOK_GOTO},
  {"GO TO",      TOK_GOTO},
  {"GOSUB",      TOK_GOSUB},
  {"RETURN",     TOK_RETURN},
  {"IF",         TOK_IF},
  {"THEN",       TOK_THEN},
  {"ELSE",       TOK_ELSE},
  {"FOR",        TOK_FOR},
  {"TO",         TOK_TO},
  {"STEP",       TOK_STEP},
  {"NEXT",       TOK_NEXT},
  {"LET",        TOK_LET},
  {"INPUT",      TOK_INPUT},
  {"LINPUT",     TOK_LINPUT},
  {"DIM",        TOK_DIM},
  {"REM",        TOK_REM},
  {"END",        TOK_END},
  {"STOP",       TOK_STOP},
  {"DATA",       TOK_DATA},
  {"READ",       TOK_READ},
  {"RESTORE",    TOK_RESTORE},
  {"RANDOMIZE",  TOK_RANDOMIZE},
  {"DEF",        TOK_DEF},
  {"ON",         TOK_ON},
  {"OPTION",     TOK_OPTION},
  {"BASE",       TOK_BASE},
  {"BREAK",      TOK_BREAK},
  {"UNBREAK",    TOK_UNBREAK},
  {"ERROR",      TOK_ERROR},
  {"WARNING",    TOK_WARNING},
  {"CONTINUE",   TOK_CONTINUE},
  {"CON",        TOK_CONTINUE},
  {"RESEQUENCE", TOK_RES},
  {"RES",        TOK_RES},
  {"SIZE",       TOK_SIZE},
  {"MERGE",      TOK_MERGE},
  {"CALL",       TOK_CALL},
  {"SUB",        TOK_SUB},
  {"SUBEND",     TOK_SUBEND},
  {"SUBEXIT",    TOK_SUBEXIT},
  {"OPEN",       TOK_OPEN},
  {"CLOSE",      TOK_CLOSE},
  {"OUTPUT",     TOK_OUTPUT},
  {"UPDATE",     TOK_UPDATE},
  {"APPEND",     TOK_APPEND},
  {"SEQUENTIAL", TOK_SEQUENTIAL},
  {"RELATIVE",   TOK_RELATIVE},
  {"INTERNAL",   TOK_INTERNAL},
  {"FIXED",      TOK_FIXED},
  {"PERMANENT",  TOK_PERMANENT},
  {"VARIABLE",   TOK_VARIABLE_KW},
  {"REC",        TOK_REC},
  {"DELETE",     TOK_DELETE},
  {"IMAGE",      TOK_IMAGE},
  {"TRACE",      TOK_TRACE},
  {"UNTRACE",    TOK_UNTRACE},
  {"AND",        TOK_AND},
  {"OR",         TOK_OR},
  {"XOR",        TOK_XOR},
  {"NOT",        TOK_NOT},
  {NULL, TOK_EOL}
};

// ---------------------------------------------------------------------------
// Tokenizer (converts text to tokens)
// ---------------------------------------------------------------------------

static int skipSpaces(const char* src, int pos)
{
  while (src[pos] == ' ')
  {
    pos++;
  }
  return pos;
}

static int matchKeyword(const char* src, int pos)
{
  int bestMatch = -1;
  int bestLen = 0;

  for (int i = 0; keywords[i].text != NULL; i++)
  {
    const char* kw = keywords[i].text;
    int klen = strlen(kw);
    bool match = true;

    for (int j = 0; j < klen; j++)
    {
      if (toupper(src[pos + j]) != kw[j])
      {
        match = false;
        break;
      }
    }

    if (match && klen > bestLen)
    {
      char next = src[pos + klen];
      if (next == '\0' || !isalnum(next))
      {
        bestMatch = i;
        bestLen = klen;
      }
    }
  }

  return bestMatch;
}

// True when the next number literal should be stored as TI-style
// TOK_LINENUM (0xC9 + big-endian word), e.g. the target of a GOTO.
static bool isLineRefToken(uint8_t tok)
{
  return tok == TOK_GOTO  || tok == TOK_GOSUB  || tok == TOK_THEN ||
         tok == TOK_ELSE  || tok == TOK_RESTORE|| tok == TOK_BREAK ||
         tok == TOK_UNBREAK || tok == TOK_RES  || tok == TOK_ERROR;
}

// Keywords that accept a single file-spec argument where the lexer
// should treat the remainder of the statement as one opaque string
// (TI-style: OLD DSK1.NAME does NOT need quotes).
static bool isFilenameKwToken(uint8_t tok)
{
  return tok == TOK_OLD   || tok == TOK_SAVE  ||
         tok == TOK_MERGE || tok == TOK_DELETE;
}

int tokenizeLine(const char* src, uint8_t* tokens, int maxLen)
{
  int pos = 0;
  int out = 0;
  bool expectLineNum = false;

  while (src[pos] != '\0')
  {
    pos = skipSpaces(src, pos);
    if (src[pos] == '\0')
    {
      break;
    }

    // REM or IMAGE — rest of line is literal text. (IMAGE captures the
    // format string verbatim so it can be looked up later by
    // PRINT USING <lineN>.)
    if (out > 0 &&
        (tokens[out - 1] == TOK_REM || tokens[out - 1] == TOK_IMAGE))
    {
      int remLen = strlen(&src[pos]);
      if (out + 2 + remLen >= maxLen)
      {
        return -1;
      }
      tokens[out++] = TOK_STRING_LIT;
      tokens[out++] = (uint8_t)remLen;
      memcpy(&tokens[out], &src[pos], remLen);
      out += remLen;
      break;
    }

    // ! — TI Extended BASIC tail comment. Stored under its own token so
    // LIST round-trips back to `!` instead of REM.
    if (src[pos] == '!')
    {
      pos++;
      int remLen = strlen(&src[pos]);
      if (out + 3 + remLen >= maxLen)
      {
        return -1;
      }
      tokens[out++] = TOK_BANG;
      tokens[out++] = TOK_STRING_LIT;
      tokens[out++] = (uint8_t)remLen;
      memcpy(&tokens[out], &src[pos], remLen);
      out += remLen;
      break;
    }

    // Quoted string
    if (src[pos] == '"')
    {
      pos++;
      int start = pos;
      while (src[pos] != '\0' && src[pos] != '"')
      {
        pos++;
      }
      int slen = pos - start;
      if (src[pos] == '"')
      {
        pos++;
      }
      if (out + 2 + slen >= maxLen)
      {
        return -1;
      }
      tokens[out++] = TOK_STRING_LIT;
      tokens[out++] = (uint8_t)slen;
      memcpy(&tokens[out], &src[start], slen);
      out += slen;
      continue;
    }

    // Number literal. In a line-reference context (after GOTO / GOSUB /
    // THEN / ELSE / RESTORE / BREAK / UNBREAK / RESEQUENCE / ON ERROR,
    // and across commas in those lists) we encode it TI-style as
    // TOK_LINENUM + 2 bytes big-endian. Otherwise it's TOK_UNQUOTED_STR
    // + ASCII digits so the expression evaluator can parse floats.
    if (isdigit(src[pos]) || (src[pos] == '.' && isdigit(src[pos + 1])))
    {
      int start = pos;
      while (isdigit(src[pos]) || src[pos] == '.' || src[pos] == 'E' ||
             src[pos] == 'e' ||
             ((src[pos] == '+' || src[pos] == '-') &&
              (src[pos - 1] == 'E' || src[pos - 1] == 'e')))
      {
        pos++;
      }
      int slen = pos - start;
      if (slen > 255 || out + 2 + slen >= maxLen)
      {
        return -1;
      }

      // Reject floats in line-ref context (line numbers are integers).
      bool hasDot = false;
      for (int i = 0; i < slen; i++)
      {
        if (src[start + i] == '.' || src[start + i] == 'E' ||
            src[start + i] == 'e') { hasDot = true; break; }
      }

      if (expectLineNum && !hasDot)
      {
        char buf[8] = {0};
        int copy = (slen < 6) ? slen : 6;
        memcpy(buf, &src[start], copy);
        unsigned long ln = strtoul(buf, NULL, 10);
        if (ln > 65535) ln = 65535;
        if (out + 3 >= maxLen) return -1;
        tokens[out++] = TOK_LINENUM;
        tokens[out++] = (uint8_t)((ln >> 8) & 0xFF);
        tokens[out++] = (uint8_t)(ln & 0xFF);
        expectLineNum = false;
        continue;
      }

      tokens[out++] = TOK_UNQUOTED_STR;
      tokens[out++] = (uint8_t)slen;
      memcpy(&tokens[out], &src[start], slen);
      out += slen;
      expectLineNum = false;
      continue;
    }

    // Operators and punctuation
    bool foundOp = true;
    bool opKeepsLineRef = false;   // only TOK_COMMA preserves expectLineNum
    switch (src[pos])
    {
      case '+':  tokens[out++] = TOK_PLUS;       pos++; break;
      case '-':  tokens[out++] = TOK_MINUS;      pos++; break;
      case '*':  tokens[out++] = TOK_MULTIPLY;   pos++; break;
      case '/':  tokens[out++] = TOK_DIVIDE;     pos++; break;
      case '^':  tokens[out++] = TOK_POWER;      pos++; break;
      case '&':  tokens[out++] = TOK_CONCAT;     pos++; break;
      case '(':  tokens[out++] = TOK_LPAREN;     pos++; break;
      case ')':  tokens[out++] = TOK_RPAREN;     pos++; break;
      case ',':  tokens[out++] = TOK_COMMA;      pos++; opKeepsLineRef = true; break;
      case ';':  tokens[out++] = TOK_SEMICOLON;  pos++; break;
      case ':':  tokens[out++] = TOK_COLON;      pos++; break;
      case '#':  tokens[out++] = TOK_HASH;       pos++; break;
      case '=':  tokens[out++] = TOK_EQUAL;      pos++; break;
      // TI encodes compound comparisons as two separate tokens, e.g.
      // <=  ->  TOK_LESS + TOK_EQUAL
      // <>  ->  TOK_LESS + TOK_GREATER
      // >=  ->  TOK_GREATER + TOK_EQUAL
      case '<':
        tokens[out++] = TOK_LESS;
        if (src[pos + 1] == '=')      { tokens[out++] = TOK_EQUAL;   pos += 2; }
        else if (src[pos + 1] == '>') { tokens[out++] = TOK_GREATER; pos += 2; }
        else                          {                              pos++;    }
        break;
      case '>':
        tokens[out++] = TOK_GREATER;
        if (src[pos + 1] == '=')      { tokens[out++] = TOK_EQUAL;   pos += 2; }
        else                          {                              pos++;    }
        break;
      default:
        foundOp = false;
        break;
    }
    if (foundOp)
    {
      expectLineNum = opKeepsLineRef ? expectLineNum : false;
      continue;
    }

    // Keyword match
    int kwIdx = matchKeyword(src, pos);
    if (kwIdx >= 0)
    {
      if (out >= maxLen)
      {
        return -1;
      }
      uint8_t emitted = keywords[kwIdx].token;
      tokens[out++] = emitted;
      pos += strlen(keywords[kwIdx].text);
      expectLineNum = isLineRefToken(emitted);

      // OLD / SAVE / MERGE / DELETE treat the rest of the statement as
      // a file-spec string, so `OLD DSK1.NAME` works without quotes
      // (TI behavior). If the user did quote it, skip — the normal
      // quoted-string branch above will have fired next iteration.
      if (isFilenameKwToken(emitted))
      {
        int sp = pos;
        while (src[sp] == ' ') sp++;
        if (src[sp] != '"' && src[sp] != '\0' &&
            src[sp] != ':' && src[sp] != '!')
        {
          int start = sp;
          while (src[sp] != '\0' && src[sp] != ':' && src[sp] != '!')
          {
            sp++;
          }
          // Trim trailing spaces
          int end = sp;
          while (end > start && src[end - 1] == ' ') end--;
          int slen = end - start;
          if (slen > 0 && out + 2 + slen < maxLen)
          {
            tokens[out++] = TOK_QUOTED_STR;
            tokens[out++] = (uint8_t)slen;
            memcpy(&tokens[out], &src[start], slen);
            out += slen;
          }
          pos = sp;
        }
      }
      continue;
    }

    // Variable name
    if (isalpha(src[pos]))
    {
      // Variable name — stored as raw ASCII bytes (TI format)
      int start = pos;
      while (isalnum(src[pos]) || src[pos] == '_')
      {
        pos++;
      }
      if (src[pos] == '$')
      {
        pos++;
      }
      int vlen = pos - start;
      if (out + vlen >= maxLen)
      {
        return -1;
      }
      for (int i = 0; i < vlen; i++)
      {
        tokens[out++] = toupper(src[start + i]);
      }
      expectLineNum = false;
      continue;
    }

    pos++;
  }

  if (out >= maxLen)
  {
    return -1;
  }
  tokens[out++] = TOK_EOL;
  return out;
}

// ---------------------------------------------------------------------------
// Detokenizer (converts tokens back to text for LIST/SAVE)
// ---------------------------------------------------------------------------

static void appendStr(char* buf, int& out, int bufSize, const char* str)
{
  int slen = strlen(str);
  int copyLen = (out + slen < bufSize - 1) ? slen : bufSize - 1 - out;
  memcpy(&buf[out], str, copyLen);
  out += copyLen;
}

int detokenizeLine(const uint8_t* tokens, int length, char* buf,
                   int bufSize)
{
  int pos = 0;
  int out = 0;

  while (pos < length && tokens[pos] != TOK_EOL)
  {
    uint8_t tok = tokens[pos++];

    // String literal
    if (tok == TOK_QUOTED_STR)
    {
      uint8_t slen = tokens[pos++];
      if (out + slen + 2 >= bufSize) break;
      buf[out++] = '"';
      memcpy(&buf[out], &tokens[pos], slen);
      out += slen;
      pos += slen;
      buf[out++] = '"';
      continue;
    }

    // Number / unquoted string
    if (tok == TOK_UNQUOTED_STR)
    {
      uint8_t slen = tokens[pos++];
      int copyLen = (out + slen < bufSize - 1) ? slen : bufSize - 1 - out;
      memcpy(&buf[out], &tokens[pos], copyLen);
      out += copyLen;
      pos += slen;
      continue;
    }

    // TI-style line-number reference (after GOTO/GOSUB/THEN/etc.)
    if (tok == TOK_LINENUM)
    {
      if (pos + 1 >= length) break;
      uint16_t ln = ((uint16_t)tokens[pos] << 8) | tokens[pos + 1];
      pos += 2;
      char num[8];
      int n = snprintf(num, sizeof(num), "%u", (unsigned)ln);
      if (out + n < bufSize - 1)
      {
        memcpy(&buf[out], num, n);
        out += n;
      }
      continue;
    }

    // Tail comment: emit "! <text>" from TOK_BANG + TOK_STRING_LIT
    if (tok == TOK_BANG)
    {
      appendStr(buf, out, bufSize, "!");
      if (pos < length && tokens[pos] == TOK_QUOTED_STR)
      {
        pos++;
        uint8_t slen = tokens[pos++];
        int copyLen = (out + slen < bufSize - 1) ? slen : bufSize - 1 - out;
        memcpy(&buf[out], &tokens[pos], copyLen);
        out += copyLen;
        pos += slen;
      }
      continue;
    }

    // Everything else: direct O(1) token -> text lookup.
    // Multi-char alphabetic names (keywords / functions) get surrounding
    // spaces; single-character entries (identifier bytes, operators,
    // punctuation) emit as-is so identifiers like "ABC" concatenate.
    const char* name = tokenNames[tok];
    if (name == NULL)
    {
      continue;
    }
    int klen = strlen(name);
    bool isMultiCharAlpha = (klen > 1 && name[0] >= 'A' && name[0] <= 'Z');
    if (out + klen + 2 >= bufSize) continue;
    if (isMultiCharAlpha && out > 0 && buf[out - 1] != ' ') buf[out++] = ' ';
    memcpy(&buf[out], name, klen);
    out += klen;
    if (isMultiCharAlpha) buf[out++] = ' ';
  }

  buf[out] = '\0';
  return out;
}

} // namespace tihost
