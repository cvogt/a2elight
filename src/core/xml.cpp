/*
 *  Albion 2 Engine "light"
 *  Copyright (C) 2004 - 2012 Florian Ziesche
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License only.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "xml.h"
#include "engine.h"
#include <libxml/encoding.h>
#include <libxml/catalog.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

/*! create and initialize the xml class
 */
xml::xml(engine* e_) : e(e_) {
	// libxml2 init
	LIBXML_TEST_VERSION
	xmlInitializeCatalog();
	xmlCatalogAdd(BAD_CAST "public",
				  BAD_CAST "-//A2E/config",
				  BAD_CAST e->data_path("dtd/config.dtd").c_str());
}

xml::~xml() {
}

xml::xml_doc xml::process_file(const string& filename, const bool validate) const {
	xml_doc doc;
	
	// read/parse/validate
	xmlParserCtxtPtr ctx = xmlNewParserCtxt();
	if(ctx == NULL) {
		a2e_error("failed to allocate parser context for \"%s\"!", filename);
		doc.valid = false;
		return doc;
	}
	
	xmlDocPtr xmldoc = xmlCtxtReadFile(ctx, filename.c_str(), NULL,
									   (validate ? XML_PARSE_DTDLOAD | XML_PARSE_DTDVALID : 0));
	if(xmldoc == NULL) {
		a2e_error("failed to parse \"%s\"!", filename);
		doc.valid = false;
		return doc;
	}
	else {
		if(ctx->valid == 0) {
			xmlFreeDoc(xmldoc);
			a2e_error("failed to validate \"%s\"!", filename);
			doc.valid = false;
			return doc;
		}
	}
	
	// create internal node structure
	deque<pair<xmlNode*, unordered_multimap<string, xml_node*>*>> node_stack;
	node_stack.push_back(make_pair(xmldoc->children, &doc.nodes));
	for(;;) {
		if(node_stack.empty()) break;
		
		xmlNode* cur_node = node_stack.front().first;
		unordered_multimap<string, xml_node*>* nodes = node_stack.front().second;
		node_stack.pop_front();
		
		for(; cur_node; cur_node = cur_node->next) {
			if(cur_node->type == XML_ELEMENT_NODE) {
				xml_node* node = new xml_node(cur_node);
				nodes->insert(make_pair(string((const char*)cur_node->name), node));
				
				if(cur_node->children != NULL) {
					node_stack.push_back(make_pair(cur_node->children, &node->children));
				}
			}
		}
	}
	
	// cleanup
	xmlFreeDoc(xmldoc);
	xmlFreeParserCtxt(ctx);
	xmlCleanupParser();
	
	return doc;
}

xml::xml_node* xml::xml_doc::get_node(const string& path) const {
	// find the node
	vector<string> levels = core::tokenize(path, '.');
	const unordered_multimap<string, xml_node*>* cur_level = &nodes;
	xml_node* cur_node = NULL;
	for(const string& level : levels) {
		const auto& next_node = cur_level->find(level);
		if(next_node == cur_level->end()) return NULL;
		cur_node = next_node->second;
		cur_level = &cur_node->children;
	}
	return cur_node;
}

const string& xml::xml_doc::extract_attr(const string& path) const {
	static const string invalid_attr = "INVALID";
	const size_t lp = path.rfind(".");
	if(lp == string::npos) return invalid_attr;
	const string node_path = path.substr(0, lp);
	const string attr_name = path.substr(lp+1, path.length()-lp-1);
	
	xml_node* node = get_node(node_path);
	if(node == NULL) return invalid_attr;
	return (*node)[attr_name];
}

template<> const string xml::xml_doc::get<string>(const string& path, const string default_value) const {
	const string& attr = extract_attr(path);
	return (attr != "INVALID" ? attr : default_value);
}
template<> const float xml::xml_doc::get<float>(const string& path, const float default_value) const {
	const string& attr = extract_attr(path);
	return (attr != "INVALID" ? strtof(attr.c_str(), NULL) : default_value);
}
template<> const size_t xml::xml_doc::get<size_t>(const string& path, const size_t default_value) const {
	const string& attr = extract_attr(path);
	return (attr != "INVALID" ? strtoull(attr.c_str(), NULL, 10) : default_value);
}
template<> const ssize_t xml::xml_doc::get<ssize_t>(const string& path, const ssize_t default_value) const {
	const string& attr = extract_attr(path);
	return (attr != "INVALID" ? strtoll(attr.c_str(), NULL, 10) : default_value);
}
template<> const bool xml::xml_doc::get<bool>(const string& path, const bool default_value) const {
	const string& attr = extract_attr(path);
	return (attr != "INVALID" ?
			(attr == "yes" || attr == "YES" ||
			 attr == "true" || attr == "TRUE" ||
			 attr == "on" || attr == "ON" || attr == "1" ? true : false) : default_value);
}
template<> const float2 xml::xml_doc::get<float2>(const string& path, const float2 default_value) const {
	const string& attr = extract_attr(path);
	vector<string> tokens= core::tokenize(attr, ',');
	return (attr != "INVALID" && tokens.size() >= 2 ?
			float2(strtof(tokens[0].c_str(), NULL), strtof(tokens[1].c_str(), NULL))
			: default_value);
}
template<> const float3 xml::xml_doc::get<float3>(const string& path, const float3 default_value) const {
	const string& attr = extract_attr(path);
	vector<string> tokens= core::tokenize(attr, ',');
	return (attr != "INVALID" && tokens.size() >= 3 ?
			float3(strtof(tokens[0].c_str(), NULL), strtof(tokens[1].c_str(), NULL), strtof(tokens[2].c_str(), NULL))
			: default_value);
}
template<> const float4 xml::xml_doc::get<float4>(const string& path, const float4 default_value) const {
	const string& attr = extract_attr(path);
	vector<string> tokens= core::tokenize(attr, ',');
	return (attr != "INVALID" && tokens.size() >= 4 ?
			float4(strtof(tokens[0].c_str(), NULL), strtof(tokens[1].c_str(), NULL), strtof(tokens[2].c_str(), NULL), strtof(tokens[3].c_str(), NULL))
			: default_value);
}
template<> const size2 xml::xml_doc::get<size2>(const string& path, const size2 default_value) const {
	const string& attr = extract_attr(path);
	vector<string> tokens= core::tokenize(attr, ',');
	return (attr != "INVALID" && tokens.size() >= 2 ?
			size2(strtoull(tokens[0].c_str(), NULL, 10),
				  strtoull(tokens[1].c_str(), NULL, 10))
			: default_value);
}
template<> const size3 xml::xml_doc::get<size3>(const string& path, const size3 default_value) const {
	const string& attr = extract_attr(path);
	vector<string> tokens= core::tokenize(attr, ',');
	return (attr != "INVALID" && tokens.size() >= 3 ?
			size3(strtoull(tokens[0].c_str(), NULL, 10),
				  strtoull(tokens[1].c_str(), NULL, 10),
				  strtoull(tokens[2].c_str(), NULL, 10))
			: default_value);
}
template<> const size4 xml::xml_doc::get<size4>(const string& path, const size4 default_value) const {
	const string& attr = extract_attr(path);
	vector<string> tokens= core::tokenize(attr, ',');
	return (attr != "INVALID" && tokens.size() >= 4 ?
			size4(strtoull(tokens[0].c_str(), NULL, 10),
				  strtoull(tokens[1].c_str(), NULL, 10),
				  strtoull(tokens[2].c_str(), NULL, 10),
				  strtoull(tokens[3].c_str(), NULL, 10))
			: default_value);
}
template<> const ssize2 xml::xml_doc::get<ssize2>(const string& path, const ssize2 default_value) const {
	const string& attr = extract_attr(path);
	vector<string> tokens= core::tokenize(attr, ',');
	return (attr != "INVALID" && tokens.size() >= 2 ?
			ssize2(strtoll(tokens[0].c_str(), NULL, 10),
				   strtoll(tokens[1].c_str(), NULL, 10))
			: default_value);
}
template<> const ssize3 xml::xml_doc::get<ssize3>(const string& path, const ssize3 default_value) const {
	const string& attr = extract_attr(path);
	vector<string> tokens= core::tokenize(attr, ',');
	return (attr != "INVALID" && tokens.size() >= 3 ?
			ssize3(strtoll(tokens[0].c_str(), NULL, 10),
				   strtoll(tokens[1].c_str(), NULL, 10),
				   strtoll(tokens[2].c_str(), NULL, 10))
			: default_value);
}
template<> const ssize4 xml::xml_doc::get<ssize4>(const string& path, const ssize4 default_value) const {
	const string& attr = extract_attr(path);
	vector<string> tokens= core::tokenize(attr, ',');
	return (attr != "INVALID" && tokens.size() >= 4 ?
			ssize4(strtoll(tokens[0].c_str(), NULL, 10),
				   strtoll(tokens[1].c_str(), NULL, 10),
				   strtoll(tokens[2].c_str(), NULL, 10),
				   strtoll(tokens[3].c_str(), NULL, 10))
			: default_value);
}

xml::xml_node::xml_node(const xmlNode* node) {
	for(xmlAttr* cur_attr = node->properties; cur_attr; cur_attr = cur_attr->next) {
		attributes.insert(make_pair(string((const char*)cur_attr->name),
									string((cur_attr->children != NULL ? (const char*)cur_attr->children->content : "INVALID"))
									));
	}
}
xml::xml_node::~xml_node() {
	// cascading delete
	for(auto& child : children) {
		delete child.second;
	}
	attributes.clear();
}