/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * rasqal_format_sv.c - Format results in CSV/TSV
 *
 * Intended to read and write the
 *   SPARQL 1.1 Query Results CSV and TSV Formats (DRAFT)
 *   http://www.w3.org/2009/sparql/docs/csv-tsv-results/results-csv-tsv.html
 * 
 * Copyright (C) 2009-2011, David Beckett http://www.dajobe.org/
 * 
 * This package is Free Software and part of Redland http://librdf.org/
 * 
 * It is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License (LGPL) V2.1 or any newer version
 *   2. GNU General Public License (GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 * 
 * You may not use this file except in compliance with at least one of
 * the above three licenses.
 * 
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * complete terms and further detail along with the license texts for
 * the licenses in COPYING.LIB, COPYING and LICENSE-2.0.txt respectively.
 * 
 * 
 */

#ifdef HAVE_CONFIG_H
#include <rasqal_config.h>
#endif

#ifdef WIN32
#include <win32_rasqal_config.h>
#endif

#include <stdio.h>
#include <string.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdarg.h>

#ifndef FILE_READ_BUF_SIZE
#ifdef BUFSIZ
#define FILE_READ_BUF_SIZE BUFSIZ
#else
#define FILE_READ_BUF_SIZE 1024
#endif
#endif

#include "rasqal.h"
#include "rasqal_internal.h"

#include "sv_config.h"

#include "sv.h"

static int
rasqal_iostream_write_csv_string(const unsigned char *string, size_t len,
                                 raptor_iostream *iostr)
{
  const char delim = '\x22';
  int quoting_needed = 0;
  size_t i;

  for(i = 0; i < len; i++) {
    char c = string[i];
    /* Quoting needed for delim (double quote), comma, linefeed or return */
    if(c == delim   || c == ',' || c == '\r' || c == '\n') {
      quoting_needed++;
      break;
    }
  }
  if(!quoting_needed)
    return raptor_iostream_counted_string_write(string, len, iostr);

  raptor_iostream_write_byte(delim, iostr);
  for(i = 0; i < len; i++) {
    char c = string[i];
    if(c == delim)
      raptor_iostream_write_byte(delim, iostr);
    raptor_iostream_write_byte(c, iostr);
  }
  raptor_iostream_write_byte(delim, iostr);

  return 0;
}

/*
 * rasqal_query_results_write_sv:
 * @iostr: #raptor_iostream to write the query to
 * @results: #rasqal_query_results query results format
 * @base_uri: #raptor_uri base URI of the output format
 * @label: name of this format for errors
 * @sep: column sep character
 * @csv_escape: non-0 if values are written escaped with CSV rules, else turtle
 * @variable_prefix: char to print before a variable name or NUL
 * @eol_str: end of line string
 * @eol_str_len: length of @eol_str
 *
 * INTERNAL - Write a @sep-separated values version of the query results format to an iostream.
 * 
 * If the writing succeeds, the query results will be exhausted.
 * 
 * Return value: non-0 on failure
 **/
static int
rasqal_query_results_write_sv(raptor_iostream *iostr,
                              rasqal_query_results* results,
                              raptor_uri *base_uri,
                              const char* label,
                              const char sep,
                              int csv_escape,
                              const char variable_prefix,
                              const char* eol_str,
                              size_t eol_str_len)
{
  rasqal_query* query = rasqal_query_results_get_query(results);
  int i;
#define empty_value_str_len 0
  static const char empty_value_str[empty_value_str_len+1] = "";
  int vars_count;
  
  if(!rasqal_query_results_is_bindings(results)) {
    rasqal_log_error_simple(query->world, RAPTOR_LOG_LEVEL_ERROR,
                            &query->locator,
                            "Can only write %s format for variable binding results",
                            label);
    return 1;
  }
  
  /* Header */
  for(i = 0; 1; i++) {
    const unsigned char *name;
    
    name = rasqal_query_results_get_binding_name(results, i);
    if(!name)
      break;

    if(i > 0)
      raptor_iostream_write_byte(sep, iostr);

    if(variable_prefix)
      raptor_iostream_write_byte(variable_prefix, iostr);
    raptor_iostream_string_write(name, iostr);
  }
  raptor_iostream_counted_string_write(eol_str, eol_str_len, iostr);


  /* Variable Binding Results */
  vars_count = rasqal_query_results_get_bindings_count(results);
  while(!rasqal_query_results_finished(results)) {
    /* Result row */
    for(i = 0; i < vars_count; i++) {
      rasqal_literal *l = rasqal_query_results_get_binding_value(results, i);

      if(i > 0)
        raptor_iostream_write_byte(sep, iostr);

      if(!l) {
        if(empty_value_str_len)
          raptor_iostream_counted_string_write(empty_value_str,
                                               empty_value_str_len, iostr);
      } else switch(l->type) {
        const unsigned char* str;
        size_t len;
        
        case RASQAL_LITERAL_URI:
          str = RASQAL_GOOD_CAST(const unsigned char*, raptor_uri_as_counted_string(l->value.uri, &len));
          if(csv_escape)
            rasqal_iostream_write_csv_string(str, len, iostr);
          else {
            raptor_iostream_write_byte('<', iostr);
            if(str && len > 0)
              raptor_string_ntriples_write(str, len, '"', iostr);
            raptor_iostream_write_byte('>', iostr);
          }
          break;

        case RASQAL_LITERAL_BLANK:
          raptor_bnodeid_ntriples_write(l->string, l->string_len, iostr);
          break;

        case RASQAL_LITERAL_STRING:
          if(csv_escape) {
            rasqal_iostream_write_csv_string(l->string, l->string_len, iostr);
          } else {
            if(l->datatype && l->valid) {
              rasqal_literal_type ltype;
              ltype = rasqal_xsd_datatype_uri_to_type(l->world, l->datatype);
              
              if(ltype >= RASQAL_LITERAL_INTEGER &&
                 ltype <= RASQAL_LITERAL_DECIMAL) {
                /* write integer, float, double and decimal XSD typed
                 * data without quotes, datatype or language 
                 */
                raptor_string_ntriples_write(l->string, l->string_len, '\0', iostr);
                break;
              }
            }

            raptor_iostream_write_byte('"', iostr);
            raptor_string_ntriples_write(l->string, l->string_len, '"', iostr);
            raptor_iostream_write_byte('"', iostr);

            if(l->language) {
              raptor_iostream_write_byte('@', iostr);
              raptor_iostream_string_write(RASQAL_GOOD_CAST(const unsigned char*, l->language), iostr);
            }
          
            if(l->datatype) {
              raptor_iostream_string_write("^^<", iostr);
              str = RASQAL_GOOD_CAST(const unsigned char*, raptor_uri_as_counted_string(l->datatype, &len));
              raptor_string_ntriples_write(str, len, '"', iostr);
              raptor_iostream_write_byte('>', iostr);
            }
          }
          
          break;

        case RASQAL_LITERAL_PATTERN:
        case RASQAL_LITERAL_QNAME:
        case RASQAL_LITERAL_INTEGER:
        case RASQAL_LITERAL_XSD_STRING:
        case RASQAL_LITERAL_BOOLEAN:
        case RASQAL_LITERAL_DOUBLE:
        case RASQAL_LITERAL_FLOAT:
        case RASQAL_LITERAL_VARIABLE:
        case RASQAL_LITERAL_DECIMAL:
        case RASQAL_LITERAL_DATE:
        case RASQAL_LITERAL_DATETIME:
        case RASQAL_LITERAL_UDT:
        case RASQAL_LITERAL_INTEGER_SUBTYPE:

        case RASQAL_LITERAL_UNKNOWN:
        default:
          rasqal_log_error_simple(query->world, RAPTOR_LOG_LEVEL_ERROR,
                                  &query->locator,
                                  "Cannot turn literal type %d into %s", 
                                  l->type, label);
      }

      /* End Binding */
    }

    /* End Result Row */
    raptor_iostream_counted_string_write(eol_str, eol_str_len, iostr);
    
    rasqal_query_results_next(results);
  }

  /* end sparql */
  return 0;
}


static int
rasqal_query_results_write_csv(rasqal_query_results_formatter* formatter,
                               raptor_iostream *iostr,
                               rasqal_query_results* results,
                               raptor_uri *base_uri)
{
  return rasqal_query_results_write_sv(iostr, results, base_uri,
                                       "CSV", ',', 1, '\0',
                                       "\r\n", 2);
}


static int
rasqal_query_results_write_tsv(rasqal_query_results_formatter* formatter,
                               raptor_iostream *iostr,
                               rasqal_query_results* results,
                               raptor_uri *base_uri)
{
  return rasqal_query_results_write_sv(iostr, results, base_uri,
                                       "TSV", '\t', 0, '?',
                                       "\n", 1);
}



typedef struct 
{
  rasqal_world* world;
  rasqal_rowsource* rowsource;
  
  int failed;

  /* Input fields */
  raptor_uri* base_uri;
  raptor_iostream* iostr;

  raptor_locator locator;

  /* SV processing */
  char sep;
  sv* t;
  char buffer[FILE_READ_BUF_SIZE]; /* iostream read buffer */
  int offset; /* current result row number */

  /* Output fields */
  raptor_sequence* results_sequence; /* saved result rows */

  /* Variables table allocated for variables in the result set */
  rasqal_variables_table* vars_table;
  int variables_count;

  unsigned int flags;
} rasqal_rowsource_sv_context;
  

static sv_status_t
rasqal_rowsource_sv_header_callback(sv *t, void *user_data,
                                    char** fields, size_t *widths,
                                    size_t count)
{
  rasqal_rowsource_sv_context* con;
  unsigned i;

  con = (rasqal_rowsource_sv_context*)user_data;

  con->variables_count = count;

  for(i = 0; i < count; i++) {
    unsigned char* var_name;
    rasqal_variable *v;
    
    var_name = RASQAL_MALLOC(unsigned char*, widths[i] + 1);
    memcpy(var_name, fields[i], widths[i] + 1);
    
    v = rasqal_variables_table_add(con->vars_table,
                                   RASQAL_VARIABLE_TYPE_NORMAL,
                                   var_name, NULL);
    if(v) {
      rasqal_rowsource_add_variable(con->rowsource, v);
      /* above function takes a reference to v */
      rasqal_free_variable(v);
    }
  }
  
  return SV_STATUS_OK;
}


static sv_status_t
rasqal_rowsource_sv_data_callback(sv *t, void *user_data,
                                  char** fields, size_t *widths,
                                  size_t count)
{
  rasqal_rowsource_sv_context* con;
  rasqal_row* row;
  unsigned i;

  con = (rasqal_rowsource_sv_context*)user_data;

  row = rasqal_new_row(con->rowsource);
  if(!row)
    goto fail;

  RASQAL_DEBUG2("Made new row %d\n", con->offset);
  con->offset++;

  for(i = 0; i < count; i++) {
    rasqal_literal* l;

    if(widths[i] > 7 && !strncmp(fields[i], "http://", 7)) {
      /* FIXME */
      raptor_uri* uri;
      uri = raptor_new_uri(con->world->raptor_world_ptr, RASQAL_GOOD_CAST(const unsigned char*, fields[i]));
      if(!uri)
        goto fail;

      l = rasqal_new_uri_literal(con->world, uri);
    } else {
      unsigned char* lvalue;

      lvalue = RASQAL_MALLOC(unsigned char*, widths[i] + 1);
      if(!lvalue)
        goto fail;

      if(!widths[i])
        *lvalue = '\0';
      else
        memcpy(lvalue, fields[i], widths[i] + 1);

      l = rasqal_new_string_literal_node(con->world, lvalue, NULL, NULL);
    }

    if(!l)
      goto fail;

    rasqal_row_set_value_at(row, i, l);
    rasqal_free_literal(l);
    RASQAL_DEBUG3("Saving row result %d string value at offset %d\n",
                  con->offset, i);
  }
  raptor_sequence_push(con->results_sequence, row);

  return SV_STATUS_OK;

  fail:
  rasqal_free_row(row);
  return SV_STATUS_NO_MEMORY;
}


static int
rasqal_rowsource_sv_init(rasqal_rowsource* rowsource, void *user_data)
{
  rasqal_rowsource_sv_context* con;

  con = (rasqal_rowsource_sv_context*)user_data;

  con->rowsource = rowsource;

  con->t = sv_init(con,
                   rasqal_rowsource_sv_header_callback,
                   rasqal_rowsource_sv_data_callback,
                   con->sep);
  if(!con->t)
    return 1;

  return 0;
}


static int
rasqal_rowsource_sv_finish(rasqal_rowsource* rowsource, void *user_data)
{
  rasqal_rowsource_sv_context* con;

  con = (rasqal_rowsource_sv_context*)user_data;

  if(con->t)
    sv_free(con->t);

  if(con->base_uri)
    raptor_free_uri(con->base_uri);

  if(con->results_sequence)
    raptor_free_sequence(con->results_sequence);

  if(con->vars_table)
    rasqal_free_variables_table(con->vars_table);

  if(con->flags) {
    if(con->iostr)
      raptor_free_iostream(con->iostr);
  }

  RASQAL_FREE(rasqal_rowsource_sv_context, con);

  return 0;
}


static void
rasqal_rowsource_sv_process(rasqal_rowsource_sv_context* con)
{
  if(raptor_sequence_size(con->results_sequence) && con->variables_count > 0)
    return;

  /* do some parsing - need some results */
  while(!raptor_iostream_read_eof(con->iostr)) {
    size_t read_len;

    read_len = raptor_iostream_read_bytes(RASQAL_GOOD_CAST(char*, con->buffer), 1,
                                          FILE_READ_BUF_SIZE,
                                          con->iostr);
    if(read_len > 0) {
      RASQAL_DEBUG2("processing %d bytes\n", RASQAL_GOOD_CAST(int, read_len));
      sv_status_t status;

      status = sv_parse_chunk(con->t, con->buffer, read_len);
      if(status != SV_STATUS_OK) {
        con->failed++;
        break;
      }
    }

    if(read_len < FILE_READ_BUF_SIZE) {
      /* finished */
      break;
    }

    /* end with variables sequence done AND at least one row */
    if(con->variables_count > 0 &&
       raptor_sequence_size(con->results_sequence) > 0)
      break;
  }
}


static int
rasqal_rowsource_sv_ensure_variables(rasqal_rowsource* rowsource,
                                             void *user_data)
{
  rasqal_rowsource_sv_context* con;

  con = (rasqal_rowsource_sv_context*)user_data;

  rasqal_rowsource_sv_process(con);

  return con->failed;
}


static rasqal_row*
rasqal_rowsource_sv_read_row(rasqal_rowsource* rowsource,
                                     void *user_data)
{
  rasqal_rowsource_sv_context* con;
  rasqal_row* row=NULL;

  con = (rasqal_rowsource_sv_context*)user_data;

  rasqal_rowsource_sv_process(con);

  if(!con->failed && raptor_sequence_size(con->results_sequence) > 0) {
    RASQAL_DEBUG1("getting row from stored sequence\n");
    row=(rasqal_row*)raptor_sequence_unshift(con->results_sequence);
  }

  return row;
}





static const rasqal_rowsource_handler rasqal_rowsource_csv_handler={
  /* .version = */ 1,
  "CSV",
  /* .init = */ rasqal_rowsource_sv_init,
  /* .finish = */ rasqal_rowsource_sv_finish,
  /* .ensure_variables = */ rasqal_rowsource_sv_ensure_variables,
  /* .read_row = */ rasqal_rowsource_sv_read_row,
  /* .read_all_rows = */ NULL,
  /* .reset = */ NULL,
  /* .set_requirements = */ NULL,
  /* .get_inner_rowsource = */ NULL,
  /* .set_origin = */ NULL,
};

static const rasqal_rowsource_handler rasqal_rowsource_tsv_handler={
  /* .version = */ 1,
  "TSV",
  /* .init = */ rasqal_rowsource_sv_init,
  /* .finish = */ rasqal_rowsource_sv_finish,
  /* .ensure_variables = */ rasqal_rowsource_sv_ensure_variables,
  /* .read_row = */ rasqal_rowsource_sv_read_row,
  /* .read_all_rows = */ NULL,
  /* .reset = */ NULL,
  /* .set_requirements = */ NULL,
  /* .get_inner_rowsource = */ NULL,
  /* .set_origin = */ NULL,
};



/*
 * rasqal_query_results_getrowsource_csv:
 * @world: rasqal world object
 * @iostr: #raptor_iostream to read the query results from
 * @base_uri: #raptor_uri base URI of the input format
 *
 * INTERNAL - Read SPARQL CSV query results format from an iostream
 * in a format returning a rowsource.
 *
 * Return value: a new rasqal_rowsource or NULL on failure
 **/
static rasqal_rowsource*
rasqal_query_results_get_rowsource_csv(rasqal_query_results_formatter* formatter,
                                       rasqal_world *world,
                                       rasqal_variables_table* vars_table,
                                       raptor_iostream *iostr,
                                       raptor_uri *base_uri,
                                       unsigned int flags)
{
  rasqal_rowsource_sv_context* con;

  con = RASQAL_CALLOC(rasqal_rowsource_sv_context*, 1, sizeof(*con));
  if(!con)
    return NULL;

  con->world = world;
  con->base_uri = base_uri ? raptor_uri_copy(base_uri) : NULL;
  con->iostr = iostr;

  con->locator.uri = base_uri;

  con->flags = flags;

  con->results_sequence = raptor_new_sequence((raptor_data_free_handler)rasqal_free_row, (raptor_data_print_handler)rasqal_row_print);

  con->vars_table = rasqal_new_variables_table_from_variables_table(vars_table);

  con->sep = ',';

  return rasqal_new_rowsource_from_handler(world, NULL,
                                           con,
                                           &rasqal_rowsource_csv_handler,
                                           con->vars_table,
                                           0);
}

/*
 * rasqal_query_results_getrowsource_tsv:
 * @world: rasqal world object
 * @iostr: #raptor_iostream to read the query results from
 * @base_uri: #raptor_uri base URI of the input format
 *
 * INTERNAL - Read SPARQL TSV query results format from an iostream
 * in a format returning a rowsource.
 *
 * Return value: a new rasqal_rowsource or NULL on failure
 **/
static rasqal_rowsource*
rasqal_query_results_get_rowsource_tsv(rasqal_query_results_formatter* formatter,
                                       rasqal_world *world,
                                       rasqal_variables_table* vars_table,
                                       raptor_iostream *iostr,
                                       raptor_uri *base_uri,
                                       unsigned int flags)
{
  rasqal_rowsource_sv_context* con;

  con = RASQAL_CALLOC(rasqal_rowsource_sv_context*, 1, sizeof(*con));
  if(!con)
    return NULL;

  con->world = world;
  con->base_uri = base_uri ? raptor_uri_copy(base_uri) : NULL;
  con->iostr = iostr;

  con->locator.uri = base_uri;

  con->flags = flags;

  con->results_sequence = raptor_new_sequence((raptor_data_free_handler)rasqal_free_row, (raptor_data_print_handler)rasqal_row_print);

  con->vars_table = rasqal_new_variables_table_from_variables_table(vars_table);

  con->sep = '\t';

  return rasqal_new_rowsource_from_handler(world, NULL,
                                           con,
                                           &rasqal_rowsource_tsv_handler,
                                           con->vars_table,
                                           0);
}





static const char* const csv_names[] = { "csv", NULL};

static const char* const csv_uri_strings[] = {
  "http://www.w3.org/ns/formats/SPARQL_Results_CSV",
  "http://www.w3.org/TR/sparql11-results-csv-tsv/",
  "http://www.ietf.org/rfc/rfc4180.txt",
  NULL
};

static const raptor_type_q csv_types[] = {
  { "text/csv", 8, 10}, 
  { "text/csv; header=present", 24, 10}, 
  { NULL, 0, 0}
};

static int
rasqal_query_results_csv_register_factory(rasqal_query_results_format_factory *factory) 
{
  int rc = 0;

  factory->desc.names = csv_names;
  factory->desc.mime_types = csv_types;

  factory->desc.label = "Comma Separated Values (CSV)";
  factory->desc.uri_strings = csv_uri_strings;

  factory->desc.flags = 0;
  
  factory->write         = rasqal_query_results_write_csv;
  factory->get_rowsource = rasqal_query_results_get_rowsource_csv;

  return rc;
}


static const char* const tsv_names[] = { "tsv", NULL};

static const char* const tsv_uri_strings[] = {
  "http://www.w3.org/ns/formats/SPARQL_Results_TSV",
  "http://www.w3.org/TR/sparql11-results-csv-tsv/",
  "http://www.iana.org/assignments/media-types/text/tab-separated-values",
  NULL
};


static const raptor_type_q tsv_types[] = {
  { "text/tab-separated-values", 25, 10}, 
  { NULL, 0, 0}
};

static int
rasqal_query_results_tsv_register_factory(rasqal_query_results_format_factory *factory) 
{
  int rc = 0;

  factory->desc.names = tsv_names;
  factory->desc.mime_types = tsv_types;

  factory->desc.label = "Tab Separated Values (TSV)";
  factory->desc.uri_strings = tsv_uri_strings;

  factory->desc.flags = 0;
  
  factory->write         = rasqal_query_results_write_tsv;
  factory->get_rowsource = rasqal_query_results_get_rowsource_tsv;

  return rc;
}


int
rasqal_init_result_format_sv(rasqal_world* world)
{
  if(!rasqal_world_register_query_results_format_factory(world,
                                                         &rasqal_query_results_csv_register_factory))
    return 1;


  if(!rasqal_world_register_query_results_format_factory(world,
                                                         &rasqal_query_results_tsv_register_factory))
    return 1;
  
  return 0;
}
