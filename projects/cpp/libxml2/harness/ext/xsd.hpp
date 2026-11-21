#pragma once
#include <string>
static const char* const XSD_PARTS[] = {
    "<xs:element name='r' type='xs:string'/>",
    "<xs:element name='r'><xs:complexType><xs:sequence><xs:element name='a' type='xs:int' minOccurs='0' maxOccurs='unbounded'/></xs:sequence><xs:attribute name='x' type='xs:boolean'/></xs:complexType></xs:element>",
    "<xs:element name='r'><xs:simpleType><xs:restriction base='xs:string'><xs:pattern value='[a-c]+\\d*'/><xs:maxLength value='5'/></xs:restriction></xs:simpleType></xs:element>",
    "<xs:element name='r'><xs:complexType><xs:choice><xs:element name='a'/><xs:element name='b' type='xs:decimal'/></xs:choice></xs:complexType></xs:element>",
    "<xs:element name='r'><xs:simpleType><xs:list itemType='xs:integer'/></xs:simpleType></xs:element>",
    "<xs:element name='r'><xs:simpleType><xs:union memberTypes='xs:date xs:boolean'/></xs:simpleType></xs:element>",
    "<xs:element name='r'><xs:complexType mixed='true'><xs:all><xs:element name='a' minOccurs='0'/><xs:element name='b' minOccurs='0'/></xs:all></xs:complexType></xs:element>",
    "<xs:element name='r' type='xs:duration'/>", "<xs:element name='r' type='xs:hexBinary'/>", "<xs:element name='r' type='xs:anyURI'/>",
    "<xs:element name='r'><xs:simpleType><xs:restriction base='xs:decimal'><xs:totalDigits value='4'/><xs:fractionDigits value='2'/><xs:minInclusive value='-10'/></xs:restriction></xs:simpleType></xs:element>",
    "<xs:element name='r'><xs:complexType><xs:sequence><xs:any processContents='lax' minOccurs='0' maxOccurs='3'/></xs:sequence><xs:anyAttribute/></xs:complexType><xs:unique name='u'><xs:selector xpath='a'/><xs:field xpath='@k'/></xs:unique></xs:element>",
};
static inline std::string xsd(int i) { return std::string("<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>") + XSD_PARTS[i] + "</xs:schema>"; }
