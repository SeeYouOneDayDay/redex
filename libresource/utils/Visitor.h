/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef _FB_ANDROID_VISITOR_H
#define _FB_ANDROID_VISITOR_H

#include <string>
#include <vector>

#include "androidfw/ResourceTypes.h"
#include "utils/ByteOrder.h"

namespace arsc {

// Helper methods regarding the serialization format.
void collect_spans(android::ResStringPool_span* ptr,
                   std::vector<android::ResStringPool_span*>* out);
// Advance the pointer past the extension to where the attributes start, unless
// there are no attributes (null pointer is returned in that case).
android::ResXMLTree_attribute* get_attribute_pointer(
    android::ResXMLTree_attrExt* extension);
// Helper methods for traversing attributes
void collect_attributes(android::ResXMLTree_attrExt* extension,
                        std::vector<android::ResXMLTree_attribute*>* out);

// Validates data is a sensible ResChunk_header struct
bool is_binary_xml(const void* data, size_t size);

// Validates that the data is a sensible ResChunk_header struct followed by
// enough data for a ResStringPool_header.
int validate_xml_string_pool(const void* data, const size_t len);

class VisitorBase {
 public:
  // Returns how far into the file this pointer is.
  long get_file_offset(const void* ptr) {
    auto delta = (const uint8_t*)ptr - (const uint8_t*)m_data;
    LOG_FATAL_IF(delta < 0, "Chunk %p is smaller than file start %p.", ptr,
                 m_data);
    LOG_FATAL_IF(delta >= (long)m_length,
                 "Chunk %p is beyond file end. Start = %p, Length = %zu", ptr,
                 m_data, m_length);
    return delta;
  }

  virtual ~VisitorBase() {}

 protected:
  void* m_data;
  size_t m_length;
};

// Class to traverse various structures in resources.arsc file. Callers should
// override various methods that are relevant for their use case. Mutating data
// is allowed, though traversal logic will not expect anything data to change
// its size (so only use this to make simple edits like changing IDs, changing
// string pool references, etc).
class ResourceTableVisitor : public VisitorBase {
 public:
  virtual bool visit(void* data, size_t len);
  virtual bool visit_table(android::ResTable_header* table);
  virtual bool visit_global_strings(android::ResStringPool_header* pool);
  // There can be many packages
  virtual bool visit_package(android::ResTable_package* package);
  virtual bool visit_type_strings(android::ResTable_package* package,
                                  android::ResStringPool_header* pool);
  virtual bool visit_key_strings(android::ResTable_package* package,
                                 android::ResStringPool_header* pool);
  // One type spec will exist per type in the package.
  virtual bool visit_type_spec(android::ResTable_package* package,
                               android::ResTable_typeSpec* type_spec);
  // Per type spec, many types can exist for the entries in various
  // configurations (i.e. differnet locales, landscape vs portrait, etc).
  virtual bool visit_type(android::ResTable_package* package,
                          android::ResTable_typeSpec* type_spec,
                          android::ResTable_type* type);
  // Visit an entry pointer, figure out what type it is and dispatch to more
  // specific visit methods. Subclasses won't need to reimplement this.
  bool begin_visit_entry(android::ResTable_package* package,
                         android::ResTable_typeSpec* type_spec,
                         android::ResTable_type* type,
                         android::ResTable_entry* entry);
  // Visit a basic entry and its value.
  virtual bool visit_entry(android::ResTable_package* package,
                           android::ResTable_typeSpec* type_spec,
                           android::ResTable_type* type,
                           android::ResTable_entry* entry,
                           android::Res_value* value);
  // Visit a map entry. Values, if any will be dispatched to separate calls.
  virtual bool visit_map_entry(android::ResTable_package* package,
                               android::ResTable_typeSpec* type_spec,
                               android::ResTable_type* type,
                               android::ResTable_map_entry* entry);
  // Visit a map entry and its value (called many times for the same
  // android::ResTable_map_entry* pointer for the N ResTable_map* pointers).
  virtual bool visit_map_value(android::ResTable_package* package,
                               android::ResTable_typeSpec* type_spec,
                               android::ResTable_type* type,
                               android::ResTable_map_entry* entry,
                               android::ResTable_map* value);
  // Callback for a chunk type that was not recognized (the format does change)
  virtual bool visit_unknown_chunk(android::ResTable_package* package,
                                   android::ResChunk_header* header);
  virtual ~ResourceTableVisitor() {}

 private:
  bool valid(const android::ResTable_package*);
  bool valid(const android::ResTable_typeSpec*);
  bool valid(const android::ResTable_type*);
};

// A visitor that can find string pool references into multiple different pools!
// This, by default will traverse Res_value structs, which index into the global
// string pool, and Entries, which index into the key strings pool.
class StringPoolRefVisitor : public ResourceTableVisitor {
 public:
  // Meant to be overridden by sub classes if they need to access/edit key
  // strings.
  virtual bool visit_key_strings_ref(android::ResTable_package* package,
                                     android::ResTable_type* type,
                                     android::ResStringPool_ref* ref);
  // Meant to be overridden by sub class if they need to access/edit global key
  // strings.
  virtual bool visit_global_strings_ref(android::Res_value* ref);
  // Meant to be overridden by sub class if they need to access/edit global key
  // strings. This method is for a style pointer into the global string pool.
  virtual bool visit_global_strings_ref(android::ResStringPool_ref* ref);
  using ResourceTableVisitor::visit_global_strings;
  bool visit_global_strings(android::ResStringPool_header* pool);
  using ResourceTableVisitor::visit_entry;
  bool visit_entry(android::ResTable_package* package,
                   android::ResTable_typeSpec* type_spec,
                   android::ResTable_type* type,
                   android::ResTable_entry* entry,
                   android::Res_value* value);
  using ResourceTableVisitor::visit_map_entry;
  bool visit_map_entry(android::ResTable_package* package,
                       android::ResTable_typeSpec* type_spec,
                       android::ResTable_type* type,
                       android::ResTable_map_entry* entry);
  using ResourceTableVisitor::visit_map_value;
  bool visit_map_value(android::ResTable_package* package,
                       android::ResTable_typeSpec* type_spec,
                       android::ResTable_type* type,
                       android::ResTable_map_entry* entry,
                       android::ResTable_map* value);
  virtual ~StringPoolRefVisitor() {}
};

class XmlFileVisitor : public VisitorBase {
 public:
  virtual bool visit(void* data, size_t len);
  virtual bool visit_global_strings(android::ResStringPool_header* pool);
  virtual bool visit_attribute_ids(uint32_t* id, size_t count);
  virtual bool visit_node(android::ResXMLTree_node* node);
  virtual bool visit_start_namespace(
      android::ResXMLTree_node* node,
      android::ResXMLTree_namespaceExt* extension);
  virtual bool visit_end_namespace(android::ResXMLTree_node* node,
                                   android::ResXMLTree_namespaceExt* extension);
  virtual bool visit_start_tag(android::ResXMLTree_node* node,
                               android::ResXMLTree_attrExt* extension);
  virtual bool visit_end_tag(android::ResXMLTree_node* node,
                             android::ResXMLTree_endElementExt* extension);
  virtual bool visit_attribute(android::ResXMLTree_node* node,
                               android::ResXMLTree_attrExt* extension,
                               android::ResXMLTree_attribute* attribute);
  virtual bool visit_cdata(android::ResXMLTree_node* node,
                           android::ResXMLTree_cdataExt* extension);
  virtual bool visit_typed_data(android::Res_value* value);
  virtual bool visit_string_ref(android::ResStringPool_ref* ref);
  virtual ~XmlFileVisitor() {}
};

class SimpleXmlParser : public arsc::XmlFileVisitor {
 public:
  ~SimpleXmlParser() override {}

  bool visit_attribute_ids(uint32_t* id, size_t count) override {
    m_attribute_count = count;
    return true;
  }

  bool visit_global_strings(android::ResStringPool_header* pool) override {
    auto status = m_global_strings.setTo(pool, dtohl(pool->header.size), true);
    LOG_ALWAYS_FATAL_IF(status != android::OK, "Invalid string pool");
    return arsc::XmlFileVisitor::visit_global_strings(pool);
  }

  android::ResStringPool& global_strings() { return m_global_strings; }
  size_t attribute_count() { return m_attribute_count; }

 private:
  size_t m_attribute_count{0};
  android::ResStringPool m_global_strings;
};
} // namespace arsc
#endif
